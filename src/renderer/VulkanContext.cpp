#include "VulkanContext.h"
#include "Swapchain.h"
#include "core/Logger.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <VkBootstrap.h>

#include <cstring>
#include <array>
#include <algorithm>

namespace skia_renderer {

// ============================================================================
// Version Detection Helpers
// ============================================================================

uint32_t VulkanContext::queryMaxSupportedInstanceVersion() {
    PFN_vkEnumerateInstanceVersion vkEnumerateInstanceVersion = 
        reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
            vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));
    
    if (vkEnumerateInstanceVersion) {
        uint32_t version = 0;
        VkResult result = vkEnumerateInstanceVersion(&version);
        if (result == VK_SUCCESS) {
            return version;
        }
    }
    
    return VK_API_VERSION_1_0;
}

bool VulkanContext::checkInstanceExtensionSupport(
    const char* extensionName,
    const std::vector<VkExtensionProperties>& availableExtensions) {
    
    return std::any_of(availableExtensions.begin(), availableExtensions.end(),
        [extensionName](const VkExtensionProperties& ext) {
            return strcmp(ext.extensionName, extensionName) == 0;
        });
}

bool VulkanContext::checkDeviceExtensionSupport(VkPhysicalDevice device, const char* extensionName) {
    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    
    std::vector<VkExtensionProperties> extensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, extensions.data());
    
    return std::any_of(extensions.begin(), extensions.end(),
        [extensionName](const VkExtensionProperties& ext) {
            return strcmp(ext.extensionName, extensionName) == 0;
        });
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

VulkanContext::VulkanContext() = default;

VulkanContext::~VulkanContext() {
    if (m_initialized) {
        shutdown();
    }
}

// ============================================================================
// Initialization with Version Negotiation
// ============================================================================

bool VulkanContext::initialize(SDL_Window* window, const VulkanVersionConfig& requestedVersion) {
    LOG_INFO("========================================");
    LOG_INFO("Vulkan Runtime Version Detection");
    LOG_INFO("========================================");
    
    m_requestedVersion = requestedVersion;
    m_capabilities.requestedVersion = requestedVersion.toVkVersion();
    
    // Query max supported instance version
    uint32_t maxInstanceVersion = queryMaxSupportedInstanceVersion();
    LOG_INFO("Max supported instance version: {}.{}.{}",
             VK_API_VERSION_MAJOR(maxInstanceVersion),
             VK_API_VERSION_MINOR(maxInstanceVersion),
             VK_API_VERSION_PATCH(maxInstanceVersion));
    
    LOG_INFO("Requested Vulkan version: {}.{}", requestedVersion.major, requestedVersion.minor);
    
    // Determine target version (try requested, then fallback)
    uint32_t targetVersion = m_capabilities.requestedVersion;
    
    if (maxInstanceVersion < targetVersion) {
        LOG_WARN("Requested Vulkan {}.{} not available (max: {}.{})", 
                 requestedVersion.major, requestedVersion.minor,
                 VK_API_VERSION_MAJOR(maxInstanceVersion),
                 VK_API_VERSION_MINOR(maxInstanceVersion));
        
        // Try Vulkan 1.3
        if (maxInstanceVersion >= VK_API_VERSION_1_3) {
            targetVersion = VK_API_VERSION_1_3;
            m_capabilities.isDowngraded = true;
            LOG_WARN("Falling back to Vulkan 1.3");
        }
        // Try Vulkan 1.2
        else if (maxInstanceVersion >= VK_API_VERSION_1_2) {
            targetVersion = VK_API_VERSION_1_2;
            m_capabilities.isDowngraded = true;
            LOG_WARN("Falling back to Vulkan 1.2");
        }
        // Try Vulkan 1.1 (minimum)
        else if (maxInstanceVersion >= VK_API_VERSION_1_1) {
            targetVersion = VK_API_VERSION_1_1;
            m_capabilities.isDowngraded = true;
            LOG_WARN("Falling back to Vulkan 1.1");
        }
        else {
            LOG_ERROR("Vulkan 1.1 is the minimum required version, but only {}.{} is available",
                     VK_API_VERSION_MAJOR(maxInstanceVersion),
                     VK_API_VERSION_MINOR(maxInstanceVersion));
            return false;
        }
    }
    
    m_capabilities.instanceApiVersion = targetVersion;
    
    // Create instance
    if (!createInstance(window)) {
        return false;
    }

    // Create device
    if (!createDevice()) {
        return false;
    }
    
    // Detect and log final capabilities
    detectFeatures();
    logCapabilities();

    // Create swapchain
    int width, height;
    SDL_GetWindowSize(window, &width, &height);
    
    m_swapchain = std::make_unique<Swapchain>();
    if (!m_swapchain->initialize(
        m_deviceInfo.physicalDevice,
        m_deviceInfo.device,
        m_surface,
        VK_NULL_HANDLE,
        width,
        height)) {
        LOG_ERROR("Failed to create swapchain");
        return false;
    }

    if (!createSyncObjects()) {
        return false;
    }

    m_initialized = true;
    LOG_INFO("========================================");
    LOG_INFO("Vulkan Context Initialized Successfully");
    LOG_INFO("========================================");
    return true;
}

void VulkanContext::shutdown() {
    if (!m_initialized) {
        return;
    }

    waitIdle();

    // Cleanup synchronization objects
    for (size_t i = 0; i < m_imageAvailableSemaphores.size(); i++) {
        if (m_imageAvailableSemaphores[i]) {
            vkDestroySemaphore(m_deviceInfo.device, m_imageAvailableSemaphores[i], nullptr);
        }
    }
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (m_inFlightFences[i]) {
            vkDestroyFence(m_deviceInfo.device, m_inFlightFences[i], nullptr);
        }
    }

    m_swapchain.reset();

    if (m_deviceInfo.device) {
        vkDestroyDevice(m_deviceInfo.device, nullptr);
        m_deviceInfo.device = VK_NULL_HANDLE;
    }

    if (m_surface) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }

    if (m_instance) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }

    m_initialized = false;
    LOG_INFO("Vulkan context destroyed.");
}

// ============================================================================
// Instance Creation
// ============================================================================

bool VulkanContext::createInstance(SDL_Window* window) {
    // Get required extensions from SDL
    Uint32 extensionCount = 0;
    const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&extensionCount);
    
    std::vector<const char*> instanceExtensions;
    for (Uint32 i = 0; i < extensionCount; i++) {
        instanceExtensions.push_back(extensions[i]);
    }

    // Add required extensions for Skia Graphite
    instanceExtensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

#ifdef ENABLE_VULKAN_VALIDATION
    instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

    LOG_DEBUG("Instance extensions ({}):", instanceExtensions.size());
    for ([[maybe_unused]] const auto& ext : instanceExtensions) {
        LOG_DEBUG("  {}", ext);
    }

    // Query available instance extensions
    uint32_t availableExtCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &availableExtCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(availableExtCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &availableExtCount, availableExtensions.data());
    
    // Check for VK_KHR_synchronization2
    m_capabilities.hasKhrSynchronization2 = 
        checkInstanceExtensionSupport(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME, availableExtensions);
    LOG_DEBUG("VK_KHR_synchronization2 extension: {}", 
             m_capabilities.hasKhrSynchronization2 ? "available" : "not available");

    uint32_t targetMajor = VK_API_VERSION_MAJOR(m_capabilities.instanceApiVersion);
    uint32_t targetMinor = VK_API_VERSION_MINOR(m_capabilities.instanceApiVersion);

    vkb::InstanceBuilder builder;
    
#ifdef ENABLE_VULKAN_VALIDATION
    auto instanceResult = builder
        .set_app_name("Skia Renderer")
        .require_api_version(targetMajor, targetMinor)
        .request_validation_layers()
        .enable_extension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME)
        .enable_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)
        .build();
#else
    auto instanceResult = builder
        .set_app_name("Skia Renderer")
        .require_api_version(targetMajor, targetMinor)
        .enable_extension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME)
        .build();
#endif

    if (!instanceResult) {
        LOG_ERROR("Failed to create Vulkan instance: {}", instanceResult.error().message());
        return false;
    }

    vkb::Instance vkbInstance = instanceResult.value();
    m_instance = vkbInstance.instance;
    m_vkbInstance = std::move(vkbInstance);

    LOG_INFO("Vulkan instance created with API version {}.{}", targetMajor, targetMinor);

    // Create surface
    if (!SDL_Vulkan_CreateSurface(window, m_instance, nullptr, &m_surface)) {
        LOG_ERROR("Failed to create Vulkan surface: {}", SDL_GetError());
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

// ============================================================================
// Device Creation
// ============================================================================

bool VulkanContext::createDevice() {
    if (m_vkbInstance.instance == VK_NULL_HANDLE) {
        LOG_ERROR("Vulkan instance not created via vk-bootstrap");
        return false;
    }
    
    vkb::PhysicalDeviceSelector selector(m_vkbInstance);
    
    uint32_t minMajor = VK_API_VERSION_MAJOR(m_capabilities.instanceApiVersion);
    uint32_t minMinor = VK_API_VERSION_MINOR(m_capabilities.instanceApiVersion);
    
    auto physicalDeviceResult = selector
        .set_surface(m_surface)
        .set_minimum_version(minMajor, minMinor)
        .add_required_extension(VK_KHR_SWAPCHAIN_EXTENSION_NAME)
        .select();

    if (!physicalDeviceResult) {
        LOG_ERROR("Failed to select physical device: {}", physicalDeviceResult.error().message());
        return false;
    }

    vkb::PhysicalDevice vkbPhysicalDevice = physicalDeviceResult.value();
    m_deviceInfo.physicalDevice = vkbPhysicalDevice.physical_device;

    // Store device properties
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_deviceInfo.physicalDevice, &props);
    m_deviceName = props.deviceName;
    m_capabilities.deviceApiVersion = props.apiVersion;
    
    LOG_INFO("Physical Device: {}", props.deviceName);
    LOG_INFO("  Device API Version: {}.{}.{}",
              VK_API_VERSION_MAJOR(props.apiVersion),
              VK_API_VERSION_MINOR(props.apiVersion),
              VK_API_VERSION_PATCH(props.apiVersion));
    LOG_INFO("  Driver Version: {}", props.driverVersion);
    LOG_INFO("  Vendor ID: 0x{:04X}", props.vendorID);
    LOG_INFO("  Device ID: 0x{:04X}", props.deviceID);

    // Check device extension support
    m_capabilities.hasKhrSynchronization2 = 
        checkDeviceExtensionSupport(m_deviceInfo.physicalDevice, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
    LOG_DEBUG("Device VK_KHR_synchronization2: {}", 
             m_capabilities.hasKhrSynchronization2 ? "supported" : "not supported");

    // Determine which features to use based on instance API version
    bool useVulkan13Features = (m_capabilities.instanceApiVersion >= VK_API_VERSION_1_3);
    
    // Vulkan 1.3 features
    VkPhysicalDeviceVulkan13Features vulkan13Features{};
    vulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    
    // Synchronization2 via extension
    VkPhysicalDeviceSynchronization2FeaturesKHR sync2Features{};
    sync2Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR;

    if (useVulkan13Features) {
        // Enable Vulkan 1.3 core features
        vulkan13Features.synchronization2 = VK_TRUE;
        vulkan13Features.dynamicRendering = VK_TRUE;
        // Enable extension features via PhysicalDevice
        vkbPhysicalDevice.enable_extension_features_if_present(vulkan13Features);
        LOG_INFO("  Requesting Vulkan 1.3 features: synchronization2, dynamicRendering");
    } else if (m_capabilities.hasKhrSynchronization2) {
        // Enable synchronization2 via extension for Vulkan 1.2
        // Must enable the extension on PhysicalDevice first
        vkbPhysicalDevice.enable_extension_if_present(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
        sync2Features.synchronization2 = VK_TRUE;
        vkbPhysicalDevice.enable_extension_features_if_present(sync2Features);
        LOG_INFO("  Requesting VK_KHR_synchronization2 extension");
    } else {
        LOG_WARN("  No synchronization2 support - will use Vulkan 1.1 fallback path");
    }

    // Build device
    vkb::DeviceBuilder deviceBuilder{vkbPhysicalDevice};

    auto deviceResult = deviceBuilder.build();
    if (!deviceResult) {
        LOG_ERROR("Failed to create device: {}", deviceResult.error().message());
        return false;
    }

    vkb::Device vkbDevice = deviceResult.value();
    m_deviceInfo.device = vkbDevice.device;

    // Get queues
    auto graphicsQueueResult = vkbDevice.get_queue(vkb::QueueType::graphics);
    auto presentQueueResult = vkbDevice.get_queue(vkb::QueueType::present);
    
    if (!graphicsQueueResult || !presentQueueResult) {
        LOG_ERROR("Failed to get device queues");
        return false;
    }

    m_deviceInfo.graphicsQueue = graphicsQueueResult.value();
    m_deviceInfo.presentQueue = presentQueueResult.value();
    m_deviceInfo.graphicsFamilyIndex = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();
    m_deviceInfo.presentFamilyIndex = vkbDevice.get_queue_index(vkb::QueueType::present).value();

    LOG_INFO("  Graphics Queue Family: {}", m_deviceInfo.graphicsFamilyIndex);
    LOG_INFO("  Present Queue Family: {}", m_deviceInfo.presentFamilyIndex);

    return true;
}

// ============================================================================
// Feature Detection
// ============================================================================

void VulkanContext::detectFeatures() {
    // Determine feature level based on INSTANCE API version
    // We cannot use features from a higher API version than our instance
    // even if the device supports them
    if (m_capabilities.instanceApiVersion >= VK_API_VERSION_1_3) {
        m_capabilities.featureLevel = VulkanFeatureLevel::Vulkan13;
        m_capabilities.hasSynchronization2 = true;
        m_capabilities.hasDynamicRendering = true;
    } else if (m_capabilities.instanceApiVersion >= VK_API_VERSION_1_2) {
        m_capabilities.featureLevel = VulkanFeatureLevel::Vulkan12;
        m_capabilities.hasSynchronization2 = m_capabilities.hasKhrSynchronization2;
        m_capabilities.hasDynamicRendering = false;
    } else if (m_capabilities.instanceApiVersion >= VK_API_VERSION_1_1) {
        m_capabilities.featureLevel = VulkanFeatureLevel::Vulkan11;
        m_capabilities.hasSynchronization2 = false;
        m_capabilities.hasDynamicRendering = false;
    } else {
        m_capabilities.featureLevel = VulkanFeatureLevel::Unknown;
        LOG_ERROR("Unsupported Vulkan version detected!");
    }
}

void VulkanContext::logCapabilities() const {
    LOG_INFO("========================================");
    LOG_INFO("Vulkan Capabilities Summary");
    LOG_INFO("========================================");
    LOG_INFO("  Requested Version: {}.{}",
             VK_API_VERSION_MAJOR(m_capabilities.requestedVersion),
             VK_API_VERSION_MINOR(m_capabilities.requestedVersion));
    LOG_INFO("  Instance Version: {}.{}",
             VK_API_VERSION_MAJOR(m_capabilities.instanceApiVersion),
             VK_API_VERSION_MINOR(m_capabilities.instanceApiVersion));
    LOG_INFO("  Device Version: {}.{}",
             VK_API_VERSION_MAJOR(m_capabilities.deviceApiVersion),
             VK_API_VERSION_MINOR(m_capabilities.deviceApiVersion));
    LOG_INFO("  Feature Level: {}", m_capabilities.getFeatureLevelString());
    LOG_INFO("  ----------------------------------------");
    LOG_INFO("  synchronization2: {}", m_capabilities.hasSynchronization2 ? "YES" : "NO");
    LOG_INFO("  dynamicRendering: {}", m_capabilities.hasDynamicRendering ? "YES" : "NO");
    LOG_INFO("  VK_KHR_synchronization2: {}", 
             m_capabilities.hasKhrSynchronization2 ? "available" : "not available");
    
    if (m_capabilities.isDowngraded) {
        LOG_WARN("  NOTE: Version was downgraded from requested version");
    }
    
    if (m_capabilities.requiresVulkan11Fallback()) {
        LOG_WARN("  NOTE: Running in Vulkan 1.1 compatibility mode");
        LOG_WARN("        Using legacy synchronization APIs");
    }
    LOG_INFO("========================================");
}

// ============================================================================
// Synchronization Objects
// ============================================================================

bool VulkanContext::createSyncObjects() {
    m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
    m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_imageToFrame.resize(m_swapchain->getImageCount(), UINT32_MAX);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateFence(m_deviceInfo.device, &fenceInfo, nullptr, 
                          &m_inFlightFences[i]) != VK_SUCCESS) {
            LOG_ERROR("Failed to create fences");
            return false;
        }
        if (vkCreateSemaphore(m_deviceInfo.device, &semaphoreInfo, nullptr, 
                              &m_imageAvailableSemaphores[i]) != VK_SUCCESS) {
            LOG_ERROR("Failed to create image available semaphores");
            return false;
        }
    }

    return true;
}

// ============================================================================
// Frame Management with Dynamic API Selection
// ============================================================================

bool VulkanContext::beginFrame() {
    if (m_frameStarted) {
        return false;
    }

    vkWaitForFences(m_deviceInfo.device, 1, &m_inFlightFences[m_currentFrame], 
                    VK_TRUE, UINT64_MAX);

    vkResetFences(m_deviceInfo.device, 1, &m_inFlightFences[m_currentFrame]);

    uint32_t imageIndex;
    VkResult result = m_swapchain->acquireNextImage(
        m_imageAvailableSemaphores[m_currentFrame],
        VK_NULL_HANDLE,
        &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        return false;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        LOG_ERROR("Failed to acquire swapchain image");
        return false;
    }

    m_currentImageIndex = imageIndex;
    m_imageToFrame[m_currentImageIndex] = m_currentFrame;

    m_frameStarted = true;
    return true;
}

void VulkanContext::endFrame(VkSemaphore renderFinishedSemaphore) {
    if (!m_frameStarted) {
        return;
    }

    // Present with synchronization
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = (renderFinishedSemaphore != VK_NULL_HANDLE) ? 1 : 0;
    presentInfo.pWaitSemaphores = &renderFinishedSemaphore;
    presentInfo.swapchainCount = 1;
    VkSwapchainKHR swapchain = m_swapchain->getSwapchain();
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &m_currentImageIndex;

    VkResult result = vkQueuePresentKHR(m_deviceInfo.presentQueue, &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        // Swapchain needs recreation
    } else if (result != VK_SUCCESS) {
        LOG_ERROR("Failed to present swapchain image");
    }

    // Signal the in-flight fence
    // Use synchronization2 API only for Vulkan 1.3 core
    // Vulkan 1.2 with VK_KHR_synchronization2 requires KHR-suffixed functions
    if (m_capabilities.supportsVulkan13()) {
        // Vulkan 1.3 core path
        vkQueueSubmit2(m_deviceInfo.graphicsQueue, 0, nullptr, m_inFlightFences[m_currentFrame]);
    } else {
        // Vulkan 1.1/1.2 fallback path
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        vkQueueSubmit(m_deviceInfo.graphicsQueue, 1, &submitInfo, m_inFlightFences[m_currentFrame]);
    }

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    m_frameStarted = false;
}

// ============================================================================
// Utility Methods
// ============================================================================

void VulkanContext::waitIdle() {
    if (m_deviceInfo.device) {
        vkDeviceWaitIdle(m_deviceInfo.device);
    }
}

void VulkanContext::resize(int width, int height) {
    waitIdle();
    m_swapchain->recreate(width, height);
}

VkExtent2D VulkanContext::getSwapchainExtent() const {
    return m_swapchain ? m_swapchain->getExtent() : VkExtent2D{0, 0};
}

VkFormat VulkanContext::getSwapchainFormat() const {
    return m_swapchain ? m_swapchain->getFormat() : VK_FORMAT_UNDEFINED;
}

VkSemaphore VulkanContext::getImageAvailableSemaphore() const {
    return m_imageAvailableSemaphores[m_currentFrame];
}

VkFence VulkanContext::getInFlightFence() const {
    return m_inFlightFences[m_currentFrame];
}

} // namespace skia_renderer
