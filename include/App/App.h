// FILE: include/App/App.h
#ifndef APP_H
#define APP_H

#include <thread>
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <cstdint>
#include <chrono>
#include <set>
#include <atomic>
#include <condition_variable>
#include <mutex>


#define GLFW_INCLUDE_VULKAN
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include "Utils/vma_usage.h" 

#include "App/AppConfig.h"
#include "App/AppState.h" 

class AudioController;
class DecoderWrapper;
class PlaybackController;
class Renderer_VK;

#include "Gui/GuiOverlay.h"
#include "Utils/ThreadSafeQueue.h"
#include "Decoder/DecoderTypes.h" 

class App {
public:
    explicit App(const std::string& filePath);
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    bool run();

    friend GuiOverlay::UIData GuiOverlay::gatherData(App* appInstance);
    friend void GuiOverlay::render(App* appInstance);
    friend void GuiOverlay::setup(GLFWwindow* window, App* appInstance);

    GLFWwindow* m_window = nullptr;
    VkInstance m_vkInstance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VmaAllocator m_vmaAllocator = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;
    VkDescriptorPool m_imguiDescriptorPool = VK_NULL_HANDLE;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;

    std::vector<std::string> m_fileList;
    int m_currentFileIndex = -1;
    std::optional<int> m_cfaOverride;
    std::string m_cfaStringFromMetadata;
    bool m_showMetrics = false;
    bool m_showHelpPage = false;

    double m_gpuWaitTimeMs = 0.0;
    double m_decodeTimeMs = 0.0;
    double m_sleepTimeMs = 0.0;
    double m_totalLoopTimeMs = 0.0;
    double m_renderPrepTimeMs = 0.0;
    double m_guiRenderTimeMs = 0.0;
    double m_vkSubmitPresentTimeMs = 0.0;
    double m_appLogicTimeMs = 0.0;

    int m_decodedWidth = 0;
    int m_decodedHeight = 0;

    PlaybackController* m_playbackController_ptr = nullptr;
    DecoderWrapper* m_decoderWrapper_ptr = nullptr;

    struct QueueFamilyIndices {
        std::optional<uint32_t> graphicsFamily;
        std::optional<uint32_t> presentFamily;
        bool isComplete() const {
            return graphicsFamily.has_value() && presentFamily.has_value();
        }
    };
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice physicalDevice);

    void handleKey(int key, int mods);
    void loadFileAtIndex(int index);
    void softDeleteCurrentFile();
    void sendCurrentFileToMotionCamFS();
    void sendAllPlaylistFilesToMotionCamFS();
    void anchorPlaybackTimeForResume();
    void recordPauseTime();
    void toggleHelpPage() { m_showHelpPage = !m_showHelpPage; }
    void saveCurrentFrameAsDng();
    void convertCurrentFileToDngs();
    void performSeek(size_t new_frame_index);
    void triggerOpenFileViaDialog();

    std::vector<VkImage> m_swapChainImages;
    std::atomic<size_t> m_activeFileLoadID{ 0 };


private:
    // Synchronization mode between video and audio after disruptive actions
    enum class PlaybackSyncState {
        NORMAL,                ///< Regular playback, video advances freely
        SEEK_AUDIO_CATCHUP,    ///< After a timeline seek – wait until audio queues up
        RESUME_AUDIO_CATCHUP   ///< After un‑pausing – wait until audio queues up
    };

    /// Current playback‑sync mode (defaults to NORMAL)
    PlaybackSyncState m_syncState{ PlaybackSyncState::NORMAL };

    // Wall‑clock timestamp (ns) of when we entered the current *_AUDIO_CATCHUP state.
    // Used to shift the wall‑clock anchor forward once audio has caught‑up.
    uint64_t          m_audioCatchupStartEpochNs{ 0 };


    ThreadSafeQueue<GpuUploadPacket> m_gpuUploadQueue{ GpuUploadQueueCapacity };
    ThreadSafeQueue<CompressedFramePacket> m_decodeQueue{ kNumPersistentStagingBuffers * DecodeQueueCapacityMultiplier };
    ThreadSafeQueue<size_t> m_availableStagingBufferIndices{ kNumPersistentStagingBuffers + AvailableStagingIndicesQueueSlack };

    std::vector<std::optional<size_t>> m_inFlightStagingBufferIndices;
    std::atomic<bool> m_hasLastSuccessfullyUploadedPacket{ false };
    GpuUploadPacket m_lastSuccessfullyUploadedPacket;

    int m_windowWidth = 1280;
    int m_windowHeight = 720;
    int m_storedWindowedPosX = 100;
    int m_storedWindowedPosY = 100;
    int m_storedWindowedWidth = 1280;
    int m_storedWindowedHeight = 720;
    bool m_isFullscreen = false;
    bool m_framebufferResized = false;

    VkSwapchainKHR m_swapChain = VK_NULL_HANDLE;
    VkFormat m_swapChainImageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D m_swapChainExtent{};
    std::vector<VkImageView> m_swapChainImageViews;
    std::vector<VkFramebuffer> m_swapChainFramebuffers;
    std::vector<VkCommandBuffer> m_commandBuffers;

    std::vector<VkSemaphore> m_imageAvailableSemaphores;
    std::vector<VkSemaphore> m_renderFinishedSemaphores;
    std::vector<VkFence> m_inFlightFences;
    uint32_t m_currentFrame = 0;

    std::unique_ptr<AudioController> m_audio;
    std::unique_ptr<DecoderWrapper> m_decoderWrapper;
    std::unique_ptr<Renderer_VK> m_rendererVk;
    std::unique_ptr<PlaybackController> m_playbackController;

    std::vector<StagingBufferInfo> m_persistentStagingBuffers;
    std::vector<void*> m_persistentStagingBuffersMappedPtrs;

    std::thread m_ioThread;
    std::thread m_decodeThread;
    std::atomic<bool> m_threadsShouldStop{ false };

    std::string m_ioThreadCurrentFilePath;
    std::mutex m_ioThreadFileMutex;
    std::condition_variable m_ioThreadFileCv;
    std::atomic<bool> m_ioThreadFileChanged{ false };

    std::atomic<size_t> m_fileLoadIDGenerator{ 0 };

    std::string m_filePath;
    int m_cfaTypeFromMetadata = 0;
    double m_staticBlack = 0.0;
    double m_staticWhite = 65535.0;
    bool m_dumpMetadata = false;

    std::chrono::steady_clock::time_point m_playbackStartTime;
    std::chrono::steady_clock::time_point m_pauseBegan;

    bool m_showUI = true;
    bool m_isPanning = false;
    double m_lastMouseX = 0.0;
    double m_lastMouseY = 0.0;
    bool m_firstFileLoaded = false;

    std::string m_lastWindowTitle;
    std::chrono::steady_clock::time_point m_lastTitleUpdateTime;

#ifdef _WIN32
    HWND _ipcWnd = nullptr;
    static LRESULT CALLBACK IpcWndProcStatic(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT IpcWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

    bool initVulkan();
    void createInstance();
    void setupDebugMessenger();
    void createSurface();
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createSwapChain();
    void createImageViews();
    void createRenderPass();
    void createFramebuffers();
    void createCommandPool();
    void createCommandBuffers();
    void createSyncObjects();
    void initImGuiVulkan();

    void createPersistentStagingBuffers();
    void destroyPersistentStagingBuffers();

    void cleanupVulkan();
    void cleanupSwapChain();
    void recreateSwapChain();

    void drawFrame();

    void ioWorkerLoop();
    void decodeWorkerLoop();
    void launchWorkerThreads();

    void handleDrop(int count, const char** paths);
    void handleMouseButton(int button, int action, int mods);
    void handleCursorPos(double xpos, double ypos);
    void framebufferSizeCallback(int width, int height);
    std::string openMcrawDialog();

    struct SwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };
    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice queryDevice);
    bool isDeviceSuitable(VkPhysicalDevice queryDevice);
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
    std::vector<const char*> getRequiredExtensions();
    bool checkDeviceExtensionSupport(VkPhysicalDevice queryDevice);

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData);

    static void framebuffer_size_callback_static(GLFWwindow* window, int w, int h);
    static void key_callback_static(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void drop_callback_static(GLFWwindow* window, int count, const char** paths);
    static void mouse_button_callback_static(GLFWwindow* window, int button, int action, int mods);
    static void cursor_pos_callback_static(GLFWwindow* window, double xpos, double ypos);
};

#endif // APP_H
