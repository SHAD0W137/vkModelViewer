#include <string>
#include <vector>
#include <iostream>
#include <chrono>

#include <imgui.h>
#include <backends/imgui_impl_SDL3.h>
#include <backends/imgui_impl_vulkan.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <glm/glm.hpp>

#ifdef IMGUI_IMPL_VULKAN_USE_VOLK
#define VOLK_IMPLEMMENTATION
#include <volk.h>
#endif



#define VK_CHECK(f) {																					\
	VkResult res = (f);																					\
	if (res != VK_SUCCESS) {																			\
		std::string message = std::format("Fatal : VkResult is ? in {} at line {}", __FILE__, __LINE__);\
		std::cout << message; exit(-1);															        \
	}																									\
}

class Renderer {
private:
	// Structures
	struct WindowFrame {
		VkCommandPool       CommandPool;
		VkCommandBuffer     CommandBuffer;
		VkFence             Fence;
		VkImage             Backbuffer;
		VkImageView         BackbufferView;
		VkFramebuffer       Framebuffer;
	};

	struct WindowFrameSemaphores {
		VkSemaphore         ImageAcquiredSemaphore;
		VkSemaphore         RenderCompleteSemaphore;
	};

	struct WindowData {
		// Input
		bool                    UseDynamicRendering;
		VkSurfaceKHR            Surface;            // Surface created and destroyed by caller.
		VkSurfaceFormatKHR      SurfaceFormat;
		VkPresentModeKHR        PresentMode;
		VkAttachmentDescription AttachmentDesc;     // RenderPass creation: main attachment description.
		VkClearValue            ClearValue;         // RenderPass creation: clear value when using VK_ATTACHMENT_LOAD_OP_CLEAR.

		// Internal
		int                     Width;              // Generally same as passed to ImGui_ImplVulkanH_CreateOrResizeWindow()
		int                     Height;
		VkSwapchainKHR          Swapchain;
		VkRenderPass            RenderPass;
		VkPipeline              Pipeline;           // The window pipeline may uses a different VkRenderPass than the one passed in ImGui_ImplVulkan_InitInfo
		uint32_t                FrameIndex;         // Current frame being rendered to (0 <= FrameIndex < FrameInFlightCount)
		uint32_t                ImageCount;         // Number of simultaneous in-flight frames (returned by vkGetSwapchainImagesKHR, usually derived from min_image_count)
		uint32_t                SemaphoreCount;     // Number of simultaneous in-flight frames + 1, to be able to use it in vkAcquireNextImageKHR
		uint32_t                SemaphoreIndex;     // Current set of swapchain wait semaphores we're using (needs to be distinct from per frame data)
		ImVector<WindowFrame>           Frames;
		ImVector<WindowFrameSemaphores> FrameSemaphores;

		WindowData() {
			memset((void*) this, 0, sizeof(*this));

			// Parameters to create SwapChain
			PresentMode = VK_PRESENT_MODE_MAX_ENUM_KHR;     // Ensure we get an error if user doesn't set this.

			// Parameters to create RenderPass
			AttachmentDesc.format = VK_FORMAT_UNDEFINED;    // Will automatically use wd->SurfaceFormat.format.
			AttachmentDesc.samples = VK_SAMPLE_COUNT_1_BIT;
			AttachmentDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			AttachmentDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			AttachmentDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			AttachmentDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			AttachmentDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			AttachmentDesc.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		}
	};

#ifdef _DEBUG
#define APP_USE_VULKAN_DEBUG_REPORT
	inline static VkDebugReportCallbackEXT g_DebugReport = VK_NULL_HANDLE;
#endif

#ifdef APP_USE_VULKAN_DEBUG_REPORT
	static VKAPI_ATTR VkBool32 VKAPI_CALL debug_report(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, uint64_t object, size_t location, int32_t messageCode, const char* pLayerPrefix, const char* pMessage, void* pUserData) {
		(void) flags; (void) object; (void) location; (void) messageCode; (void) pUserData; (void) pLayerPrefix; // Unused arguments
		fprintf(stderr, "[vulkan] Debug report from ObjectType: %i\nMessage: %s\n\n", objectType, pMessage);
		return VK_FALSE;
	}
#endif // APP_USE_VULKAN_DEBUG_REPORT

	inline static VkAllocationCallbacks* g_Allocator = nullptr;
	inline static VkInstance             g_Instance { VK_NULL_HANDLE };
	inline static VkPhysicalDevice       g_PhysicalDevice = VK_NULL_HANDLE;
	inline static VkDevice               g_Device = VK_NULL_HANDLE;
	inline static uint32_t               g_QueueFamily = (uint32_t) -1;
	inline static VkQueue                g_Queue = VK_NULL_HANDLE;
	inline static VkPipelineCache        g_PipelineCache = VK_NULL_HANDLE;
	inline static VkDescriptorPool       g_DescriptorPool = VK_NULL_HANDLE;

	inline static WindowData			 g_MainWindowData;
	inline static uint32_t               g_MinImageCount = 2;
	inline static bool					 g_SwapChainRebuild = false;

	static void setupVulkan(std::vector<const char*> instance_extensions);
	static void setupVulkanWindow(WindowData* wd, VkSurfaceKHR surface, int width, int height);
	static VkPhysicalDevice selectPhysicalDevice(VkInstance instance);
	static uint32_t selectQueueFamilyIndex(VkPhysicalDevice physical_device);
	static VkSurfaceFormatKHR selectSurfaceFormat(VkPhysicalDevice physical_device, VkSurfaceKHR surface, const VkFormat* request_formats, int request_formats_count, VkColorSpaceKHR request_color_space);
	static VkPresentModeKHR selectPresentMode(VkPhysicalDevice physical_device, VkSurfaceKHR surface, const VkPresentModeKHR* request_modes, int request_modes_count);
	static void createOrResizeWindow(VkInstance instance, VkPhysicalDevice physical_device, VkDevice device, WindowData* wd, uint32_t queue_family,
									  const VkAllocationCallbacks* allocator, int width, int height, uint32_t min_image_count, VkImageUsageFlags image_usage);
	static void createWindowSwapChain(VkPhysicalDevice physical_device, VkDevice device, WindowData* wd,
									  const VkAllocationCallbacks* allocator, int w, int h, uint32_t min_image_count, VkImageUsageFlags image_usage);
	static void createWindowCommandBuffers(VkPhysicalDevice physical_device, VkDevice device, WindowData* wd, uint32_t queue_family, const VkAllocationCallbacks* allocator);
	
	static void frameRender(WindowData* wd, ImDrawData* draw_data);
	static void framePresent(WindowData* wd);
	static void destroyFrame(VkDevice device, WindowFrame* fd, const VkAllocationCallbacks* allocator);
	static void destroyFrameSemaphores(VkDevice device, WindowFrameSemaphores* fsd, const VkAllocationCallbacks* allocator);

	static void destroyWindow(VkInstance instance, VkDevice device, WindowData* wd, const VkAllocationCallbacks* allocator);
	static void cleanupVulkan();
	static void cleanupVulkanWindow(WindowData* wd);

	static bool isExtensionAvailable(const ImVector<VkExtensionProperties>& properties, const char* extension);

	static int getMinImageCountFromPresentMode(VkPresentModeKHR present_mode);

public:
	static int run();
};