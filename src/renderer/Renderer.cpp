#include "Renderer.h"

bool Renderer::isExtensionAvailable(const ImVector<VkExtensionProperties>& properties, const char* extension) {
	return false;
}

void Renderer::setupVulkan(std::vector<const char*> instance_extensions) {
	VkResult err;
#ifdef IMGUI_IMPL_VULKAN_USE_VOLK
	volkInitialize();
#endif

	// Create Vulkan Instance
	{
		VkInstanceCreateInfo create_info = {};
		create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;

		// Enumerate available extensions
		uint32_t properties_count;
		ImVector<VkExtensionProperties> properties;
		vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, nullptr);
		properties.resize(properties_count);
		err = vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, properties.Data);
		VK_CHECK(err);

		// Enable required extensions
		if (isExtensionAvailable(properties, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
			instance_extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
		if (isExtensionAvailable(properties, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
			instance_extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
			create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
		}
#endif

		// Enabling validation layers
#ifdef APP_USE_VULKAN_DEBUG_REPORT
		const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
		create_info.enabledLayerCount = 1;
		create_info.ppEnabledLayerNames = layers;
		instance_extensions.push_back("VK_EXT_debug_report");
#endif

		// Create Vulkan Instance
		create_info.enabledExtensionCount = (uint32_t) instance_extensions.size();
		create_info.ppEnabledExtensionNames = instance_extensions.data();
		err = vkCreateInstance(&create_info, g_Allocator, &g_Instance);
		VK_CHECK(err);
#ifdef IMGUI_IMPL_VULKAN_USE_VOLK
		volkLoadInstance(g_Instance);
#endif

		// Setup the debug report callback
#ifdef APP_USE_VULKAN_DEBUG_REPORT
		auto f_vkCreateDebugReportCallbackEXT = (PFN_vkCreateDebugReportCallbackEXT) vkGetInstanceProcAddr(g_Instance, "vkCreateDebugReportCallbackEXT");
		assert(f_vkCreateDebugReportCallbackEXT != nullptr);
		VkDebugReportCallbackCreateInfoEXT debug_report_ci = {};
		debug_report_ci.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
		debug_report_ci.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
		debug_report_ci.pfnCallback = debug_report;
		debug_report_ci.pUserData = nullptr;
		err = f_vkCreateDebugReportCallbackEXT(g_Instance, &debug_report_ci, g_Allocator, &g_DebugReport);
		VK_CHECK(err);
#endif
	}

	// Select Physical Device (GPU)
	g_PhysicalDevice = selectPhysicalDevice(g_Instance);
	assert(g_PhysicalDevice != VK_NULL_HANDLE);

	// Select graphics queue family
	g_QueueFamily = selectQueueFamilyIndex(g_PhysicalDevice);
	assert(g_QueueFamily != (uint32_t) -1);

	// Create Logical Device (with 1 queue)
	{
		std::vector<const char*> device_extensions;
		device_extensions.push_back("VK_KHR_swapchain");

		// Enumerate physical device extension
		uint32_t properties_count;
		ImVector<VkExtensionProperties> properties;
		vkEnumerateDeviceExtensionProperties(g_PhysicalDevice, nullptr, &properties_count, nullptr);
		properties.resize(properties_count);
		vkEnumerateDeviceExtensionProperties(g_PhysicalDevice, nullptr, &properties_count, properties.Data);

#ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
		if (IsExtensionAvailable(properties, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME))
			device_extensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
#endif

		const float queue_priority[] = { 1.0f };
		VkDeviceQueueCreateInfo queue_info[1] = {};
		queue_info[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queue_info[0].queueFamilyIndex = g_QueueFamily;
		queue_info[0].queueCount = 1;
		queue_info[0].pQueuePriorities = queue_priority;
		VkDeviceCreateInfo create_info = {};
		create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		create_info.queueCreateInfoCount = sizeof(queue_info) / sizeof(queue_info[0]);
		create_info.pQueueCreateInfos = queue_info;
		create_info.enabledExtensionCount = (uint32_t) device_extensions.size();
		create_info.ppEnabledExtensionNames = device_extensions.data();
		err = vkCreateDevice(g_PhysicalDevice, &create_info, g_Allocator, &g_Device);
		VK_CHECK(err);
		vkGetDeviceQueue(g_Device, g_QueueFamily, 0, &g_Queue);
	}

	// Create Descriptor Pool
	// If you wish to load e.g. additional textures you may need to alter pools sizes and maxSets.
	{
		VkDescriptorPoolSize pool_sizes[] =
		{
			{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE },
			{ VK_DESCRIPTOR_TYPE_SAMPLER, IMGUI_IMPL_VULKAN_MINIMUM_SAMPLER_POOL_SIZE },
		};
		VkDescriptorPoolCreateInfo pool_info = {};
		pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		pool_info.maxSets = 0;
		for (VkDescriptorPoolSize& pool_size : pool_sizes)
			pool_info.maxSets += pool_size.descriptorCount;
		pool_info.poolSizeCount = (uint32_t) IM_COUNTOF(pool_sizes);
		pool_info.pPoolSizes = pool_sizes;
		err = vkCreateDescriptorPool(g_Device, &pool_info, g_Allocator, &g_DescriptorPool);
		VK_CHECK(err);
	}
}

VkPhysicalDevice Renderer::selectPhysicalDevice(VkInstance instance) {
	uint32_t gpu_count;
	VK_CHECK(vkEnumeratePhysicalDevices(instance, &gpu_count, nullptr));
	IM_ASSERT(gpu_count > 0);

	ImVector<VkPhysicalDevice> gpus;
	gpus.resize(gpu_count);
	VK_CHECK(vkEnumeratePhysicalDevices(instance, &gpu_count, gpus.Data));

	// If a number >1 of GPUs got reported, find discrete GPU if present, or use first one available.
	for (VkPhysicalDevice& device : gpus) {
		VkPhysicalDeviceProperties properties;
		vkGetPhysicalDeviceProperties(device, &properties);
		if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
			return device;
	}

	// Use first GPU (Integrated) is a Discrete one is not available.
	if (gpu_count > 0)
		return gpus[0];
	return VK_NULL_HANDLE;
}

uint32_t Renderer::selectQueueFamilyIndex(VkPhysicalDevice physical_device) {
	uint32_t count;
	vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, nullptr);
	std::vector<VkQueueFamilyProperties> queues_properties;
	queues_properties.resize((int) count);
	vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, queues_properties.data());
	for (uint32_t i = 0; i < count; i++)
		if (queues_properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
			return i;
	return (uint32_t) -1;
}

void Renderer::setupVulkanWindow(WindowData* wd, VkSurfaceKHR surface, int width, int height) {
	// Check for WSI support
	VkBool32 res;
	vkGetPhysicalDeviceSurfaceSupportKHR(g_PhysicalDevice, g_QueueFamily, surface, &res);
	if (res != VK_TRUE) {
		fprintf(stderr, "Error no WSI support on physical device 0\n");
		exit(-1);
	}

	// Select Surface Format
	const VkFormat requestSurfaceImageFormat[] = { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM };
	const VkColorSpaceKHR requestSurfaceColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
	wd->Surface = surface;
	wd->SurfaceFormat = selectSurfaceFormat(g_PhysicalDevice, wd->Surface, requestSurfaceImageFormat, sizeof(requestSurfaceImageFormat), requestSurfaceColorSpace);

	// Select Present Mode
#ifdef APP_USE_UNLIMITED_FRAME_RATE
	VkPresentModeKHR present_modes[] = { VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_FIFO_KHR };
#else
	VkPresentModeKHR present_modes[] = { VK_PRESENT_MODE_FIFO_KHR };
#endif
	wd->PresentMode = selectPresentMode(g_PhysicalDevice, wd->Surface, &present_modes[0], IM_COUNTOF(present_modes));
	printf("[vulkan] Selected PresentMode = %d\n", wd->PresentMode);

	// Create SwapChain, RenderPass, Framebuffer, etc.
	assert(g_MinImageCount >= 2);
	createOrResizeWindow(g_Instance, g_PhysicalDevice, g_Device, wd, g_QueueFamily, g_Allocator, width, height, g_MinImageCount, 0);

}

VkSurfaceFormatKHR Renderer::selectSurfaceFormat(VkPhysicalDevice physical_device, VkSurfaceKHR surface, const VkFormat* request_formats, int request_formats_count, VkColorSpaceKHR request_color_space) {
	//IM_ASSERT(g_FunctionsLoaded && "Need to call ImGui_ImplVulkan_LoadFunctions() if IMGUI_IMPL_VULKAN_NO_PROTOTYPES or VK_NO_PROTOTYPES are set!");
	assert(request_formats != nullptr);
	assert(request_formats_count > 0);

	// Per Spec Format and View Format are expected to be the same unless VK_IMAGE_CREATE_MUTABLE_BIT was set at image creation
	// Assuming that the default behavior is without setting this bit, there is no need for separate Swapchain image and image view format
	// Additionally several new color spaces were introduced with Vulkan Spec v1.0.40,
	// hence we must make sure that a format with the mostly available color space, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, is found and used.
	uint32_t avail_count;
	vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &avail_count, nullptr);
	ImVector<VkSurfaceFormatKHR> avail_format;
	avail_format.resize((int) avail_count);
	vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &avail_count, avail_format.Data);

	// First check if only one format, VK_FORMAT_UNDEFINED, is available, which would imply that any format is available
	if (avail_count == 1) {
		if (avail_format[0].format == VK_FORMAT_UNDEFINED) {
			VkSurfaceFormatKHR ret;
			ret.format = request_formats[0];
			ret.colorSpace = request_color_space;
			return ret;
		} else {
			// No point in searching another format
			return avail_format[0];
		}
	} else {
		// Request several formats, the first found will be used
		for (int request_i = 0; request_i < request_formats_count; request_i++)
			for (uint32_t avail_i = 0; avail_i < avail_count; avail_i++)
				if (avail_format[avail_i].format == request_formats[request_i] && avail_format[avail_i].colorSpace == request_color_space)
					return avail_format[avail_i];

		// If none of the requested image formats could be found, use the first available
		return avail_format[0];
	}
}

VkPresentModeKHR Renderer::selectPresentMode(VkPhysicalDevice physical_device, VkSurfaceKHR surface, const VkPresentModeKHR* request_modes, int request_modes_count) {
	//IM_ASSERT(g_FunctionsLoaded && "Need to call ImGui_ImplVulkan_LoadFunctions() if IMGUI_IMPL_VULKAN_NO_PROTOTYPES or VK_NO_PROTOTYPES are set!");
	assert(request_modes != nullptr);
	assert(request_modes_count > 0);

	// Request a certain mode and confirm that it is available. If not use VK_PRESENT_MODE_FIFO_KHR which is mandatory
	uint32_t avail_count = 0;
	vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &avail_count, nullptr);
	std::vector<VkPresentModeKHR> avail_modes;
	avail_modes.resize((int) avail_count);
	vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &avail_count, avail_modes.data());
	//for (uint32_t avail_i = 0; avail_i < avail_count; avail_i++)
	//    printf("[vulkan] avail_modes[%d] = %d\n", avail_i, avail_modes[avail_i]);

	for (int request_i = 0; request_i < request_modes_count; request_i++)
		for (uint32_t avail_i = 0; avail_i < avail_count; avail_i++)
			if (request_modes[request_i] == avail_modes[avail_i])
				return request_modes[request_i];

	return VK_PRESENT_MODE_FIFO_KHR; // Always available
}

void Renderer::cleanupVulkan() {
	vkDestroyDescriptorPool(g_Device, g_DescriptorPool, g_Allocator);

#ifdef APP_USE_VULKAN_DEBUG_REPORT
	// Remove the debug report callback
	auto f_vkDestroyDebugReportCallbackEXT = (PFN_vkDestroyDebugReportCallbackEXT) vkGetInstanceProcAddr(g_Instance, "vkDestroyDebugReportCallbackEXT");
	f_vkDestroyDebugReportCallbackEXT(g_Instance, g_DebugReport, g_Allocator);
#endif // APP_USE_VULKAN_DEBUG_REPORT

	vkDestroyDevice(g_Device, g_Allocator);
	vkDestroyInstance(g_Instance, g_Allocator);
}

void Renderer::destroyWindow(VkInstance instance, VkDevice device, WindowData* wd, const VkAllocationCallbacks* allocator) {
	IM_UNUSED(instance);
	vkDeviceWaitIdle(device); // FIXME: We could wait on the Queue if we had the queue in wd-> (otherwise VulkanH functions can't use globals)
	//vkQueueWaitIdle(bd->Queue);

	for (uint32_t i = 0; i < wd->ImageCount; i++)
		destroyFrame(device, &wd->Frames[i], allocator);
	for (uint32_t i = 0; i < wd->SemaphoreCount; i++)
		destroyFrameSemaphores(device, &wd->FrameSemaphores[i], allocator);
	wd->Frames.clear();
	wd->FrameSemaphores.clear();
	vkDestroyPipeline(device, wd->Pipeline, allocator);
	vkDestroyRenderPass(device, wd->RenderPass, allocator);
	vkDestroySwapchainKHR(device, wd->Swapchain, allocator);
	wd->RenderPass = VK_NULL_HANDLE;
	wd->Swapchain = VK_NULL_HANDLE;
	wd->Width = wd->Height = 0;
	wd->FrameIndex = wd->ImageCount = wd->SemaphoreCount = wd->SemaphoreIndex = 0;
	//vkDestroySurfaceKHR(instance, wd->Surface, allocator); // v1.92.6 (~2026-01-16): because wd->Surface is user provided we don't attempt to destroy it ourself.
}

void Renderer::cleanupVulkanWindow(WindowData* wd) {
	destroyWindow(g_Instance, g_Device, wd, g_Allocator);
	vkDestroySurfaceKHR(g_Instance, wd->Surface, g_Allocator);
}

void Renderer::frameRender(WindowData* wd, ImDrawData* draw_data) {
	VkSemaphore image_acquired_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
	VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
	VkResult err = vkAcquireNextImageKHR(g_Device, wd->Swapchain, UINT64_MAX, image_acquired_semaphore, VK_NULL_HANDLE, &wd->FrameIndex);
	if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
		g_SwapChainRebuild = true;
	if (err == VK_ERROR_OUT_OF_DATE_KHR)
		return;
	if (err != VK_SUBOPTIMAL_KHR)
		VK_CHECK(err);

	WindowFrame* fd = &wd->Frames[wd->FrameIndex];{
		VK_CHECK(vkWaitForFences(g_Device, 1, &fd->Fence, VK_TRUE, UINT64_MAX));    // wait indefinitely instead of periodically checking

		VK_CHECK(vkResetFences(g_Device, 1, &fd->Fence));
	}
	{
		VK_CHECK(vkResetCommandPool(g_Device, fd->CommandPool, 0));
		VkCommandBufferBeginInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		VK_CHECK(vkBeginCommandBuffer(fd->CommandBuffer, &info));
	}
	{
		VkRenderPassBeginInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		info.renderPass = wd->RenderPass;
		info.framebuffer = fd->Framebuffer;
		info.renderArea.extent.width = wd->Width;
		info.renderArea.extent.height = wd->Height;
		info.clearValueCount = 1;
		info.pClearValues = &wd->ClearValue;
		vkCmdBeginRenderPass(fd->CommandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
	}

	// Record dear imgui primitives into command buffer
	ImGui_ImplVulkan_RenderDrawData(draw_data, fd->CommandBuffer);

	// Submit command buffer
	vkCmdEndRenderPass(fd->CommandBuffer);
	{
		VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		VkSubmitInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		info.waitSemaphoreCount = 1;
		info.pWaitSemaphores = &image_acquired_semaphore;
		info.pWaitDstStageMask = &wait_stage;
		info.commandBufferCount = 1;
		info.pCommandBuffers = &fd->CommandBuffer;
		info.signalSemaphoreCount = 1;
		info.pSignalSemaphores = &render_complete_semaphore;

		VK_CHECK(vkEndCommandBuffer(fd->CommandBuffer));
		VK_CHECK(vkQueueSubmit(g_Queue, 1, &info, fd->Fence));
	}
}

void Renderer::framePresent(WindowData* wd) {
	if (g_SwapChainRebuild)
		return;
	VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
	VkPresentInfoKHR info = {};
	info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	info.waitSemaphoreCount = 1;
	info.pWaitSemaphores = &render_complete_semaphore;
	info.swapchainCount = 1;
	info.pSwapchains = &wd->Swapchain;
	info.pImageIndices = &wd->FrameIndex;
	VkResult err = vkQueuePresentKHR(g_Queue, &info);
	if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
		g_SwapChainRebuild = true;
	if (err == VK_ERROR_OUT_OF_DATE_KHR)
		return;
	if (err != VK_SUBOPTIMAL_KHR)
		VK_CHECK(err);
	wd->SemaphoreIndex = (wd->SemaphoreIndex + 1) % wd->SemaphoreCount; // Now we can use the next set of semaphores
}

void Renderer::createWindowSwapChain(VkPhysicalDevice physical_device, VkDevice device, WindowData* wd, const VkAllocationCallbacks* allocator, int w, int h, uint32_t min_image_count, VkImageUsageFlags image_usage) {
	VkResult err;
	VkSwapchainKHR old_swapchain = wd->Swapchain;
	wd->Swapchain = VK_NULL_HANDLE;
	err = vkDeviceWaitIdle(device);
	VK_CHECK(err);

	// We don't use ImGui_ImplVulkanH_DestroyWindow() because we want to preserve the old swapchain to create the new one.
	// Destroy old Framebuffer
	for (uint32_t i = 0; i < wd->ImageCount; i++)
		destroyFrame(device, &wd->Frames[i], allocator);
	for (uint32_t i = 0; i < wd->SemaphoreCount; i++)
		destroyFrameSemaphores(device, &wd->FrameSemaphores[i], allocator);
	wd->Frames.clear();
	wd->FrameSemaphores.clear();
	wd->ImageCount = 0;
	if (wd->RenderPass)
		vkDestroyRenderPass(device, wd->RenderPass, allocator);
	if (wd->Pipeline)
		vkDestroyPipeline(device, wd->Pipeline, allocator);

	// If min image count was not specified, request different count of images dependent on selected present mode
	if (min_image_count == 0)
		min_image_count = getMinImageCountFromPresentMode(wd->PresentMode);

	// Create Swapchain
	{
		VkSurfaceCapabilitiesKHR cap;
		VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, wd->Surface, &cap));

		VkSwapchainCreateInfoKHR info = {};
		info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		info.surface = wd->Surface;
		info.minImageCount = min_image_count;
		info.imageFormat = wd->SurfaceFormat.format;
		info.imageColorSpace = wd->SurfaceFormat.colorSpace;
		info.imageArrayLayers = 1;
		info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | image_usage;
		info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;           // Assume that graphics family == present family
		info.preTransform = (cap.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : cap.currentTransform;
		if (cap.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
			info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		else if (cap.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)
			info.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
		else
			IM_ASSERT(false && "No supported composite alpha mode found!");
		info.presentMode = wd->PresentMode;
		info.clipped = VK_TRUE;
		info.oldSwapchain = old_swapchain;
		if (info.minImageCount < cap.minImageCount)
			info.minImageCount = cap.minImageCount;
		else if (cap.maxImageCount != 0 && info.minImageCount > cap.maxImageCount)
			info.minImageCount = cap.maxImageCount;
		if (cap.currentExtent.width == 0xffffffff) {
			info.imageExtent.width = wd->Width = w;
			info.imageExtent.height = wd->Height = h;
		} else {
			info.imageExtent.width = wd->Width = cap.currentExtent.width;
			info.imageExtent.height = wd->Height = cap.currentExtent.height;
		}
		VK_CHECK(vkCreateSwapchainKHR(device, &info, allocator, &wd->Swapchain));
		VK_CHECK(vkGetSwapchainImagesKHR(device, wd->Swapchain, &wd->ImageCount, nullptr));
		VkImage backbuffers[16] = {};
		assert(wd->ImageCount >= min_image_count);
		assert(wd->ImageCount < IM_COUNTOF(backbuffers));
		VK_CHECK(vkGetSwapchainImagesKHR(device, wd->Swapchain, &wd->ImageCount, backbuffers));

		wd->SemaphoreCount = wd->ImageCount + 1;
		wd->Frames.resize(wd->ImageCount);
		wd->FrameSemaphores.resize(wd->SemaphoreCount);
		memset(wd->Frames.Data, 0, wd->Frames.size_in_bytes());
		memset(wd->FrameSemaphores.Data, 0, wd->FrameSemaphores.size_in_bytes());
		for (uint32_t i = 0; i < wd->ImageCount; i++)
			wd->Frames[i].Backbuffer = backbuffers[i];
	}
	if (old_swapchain)
		vkDestroySwapchainKHR(device, old_swapchain, allocator);

	// Create the Render Pass
	if (wd->UseDynamicRendering == false) {
		VkAttachmentDescription attachment = wd->AttachmentDesc;
		if (attachment.format == VK_FORMAT_UNDEFINED)
			attachment.format = wd->SurfaceFormat.format;

		VkAttachmentReference color_attachment = {};
		color_attachment.attachment = 0;
		color_attachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass = {};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &color_attachment;

		VkSubpassDependency dependency = {};
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;
		dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.srcAccessMask = 0;
		dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		VkRenderPassCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		info.attachmentCount = 1;
		info.pAttachments = &attachment;
		info.subpassCount = 1;
		info.pSubpasses = &subpass;
		info.dependencyCount = 1;
		info.pDependencies = &dependency;
		err = vkCreateRenderPass(device, &info, allocator, &wd->RenderPass);
		VK_CHECK(err);

		// We do not create a pipeline by default as this is also used by examples' main.cpp,
		// but secondary viewport in multi-viewport mode may want to create one with:
		//ImGui_ImplVulkan_CreatePipeline(device, allocator, VK_NULL_HANDLE, wd->RenderPass, VK_SAMPLE_COUNT_1_BIT, &wd->Pipeline, v->Subpass);
	}

	// Create The Image Views
	{
		VkImageViewCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		info.format = wd->SurfaceFormat.format;
		info.components.r = VK_COMPONENT_SWIZZLE_R;
		info.components.g = VK_COMPONENT_SWIZZLE_G;
		info.components.b = VK_COMPONENT_SWIZZLE_B;
		info.components.a = VK_COMPONENT_SWIZZLE_A;

		VkImageSubresourceRange image_range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		info.subresourceRange = image_range;
		for (uint32_t i = 0; i < wd->ImageCount; i++) {
			WindowFrame* fd = &wd->Frames[i];
			info.image = fd->Backbuffer;
			err = vkCreateImageView(device, &info, allocator, &fd->BackbufferView);
			VK_CHECK(err);
		}
	}

	// Create Framebuffer
	if (wd->UseDynamicRendering == false) {
		VkImageView attachment[1];
		VkFramebufferCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		info.renderPass = wd->RenderPass;
		info.attachmentCount = 1;
		info.pAttachments = attachment;
		info.width = wd->Width;
		info.height = wd->Height;
		info.layers = 1;
		for (uint32_t i = 0; i < wd->ImageCount; i++) {
			WindowFrame* fd = &wd->Frames[i];
			attachment[0] = fd->BackbufferView;
			err = vkCreateFramebuffer(device, &info, allocator, &fd->Framebuffer);
			VK_CHECK(err);
		}
	}
}

int Renderer::getMinImageCountFromPresentMode(VkPresentModeKHR present_mode) {
	if (present_mode == VK_PRESENT_MODE_MAILBOX_KHR)
		return 3;
	if (present_mode == VK_PRESENT_MODE_FIFO_KHR || present_mode == VK_PRESENT_MODE_FIFO_RELAXED_KHR)
		return 2;
	if (present_mode == VK_PRESENT_MODE_IMMEDIATE_KHR)
		return 1;
	IM_ASSERT(0);
	return 1;
}

void Renderer::createWindowCommandBuffers(VkPhysicalDevice physical_device, VkDevice device, WindowData* wd, uint32_t queue_family, const VkAllocationCallbacks* allocator) {
	IM_ASSERT(physical_device != VK_NULL_HANDLE && device != VK_NULL_HANDLE);
	IM_UNUSED(physical_device);

	// Create Command Buffers
	VkResult err;
	for (uint32_t i = 0; i < wd->ImageCount; i++) {
		WindowFrame* fd = &wd->Frames[i];
		{
			VkCommandPoolCreateInfo info = {};
			info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			info.flags = 0;
			info.queueFamilyIndex = queue_family;
			err = vkCreateCommandPool(device, &info, allocator, &fd->CommandPool);
			VK_CHECK(err);
		}
		{
			VkCommandBufferAllocateInfo info = {};
			info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			info.commandPool = fd->CommandPool;
			info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			info.commandBufferCount = 1;
			err = vkAllocateCommandBuffers(device, &info, &fd->CommandBuffer);
			VK_CHECK(err);
		}
		{
			VkFenceCreateInfo info = {};
			info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
			err = vkCreateFence(device, &info, allocator, &fd->Fence);
			VK_CHECK(err);
		}
	}

	for (uint32_t i = 0; i < wd->SemaphoreCount; i++) {
		WindowFrameSemaphores* fsd = &wd->FrameSemaphores[i];
		{
			VkSemaphoreCreateInfo info = {};
			info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
			err = vkCreateSemaphore(device, &info, allocator, &fsd->ImageAcquiredSemaphore);
			VK_CHECK(err);
			err = vkCreateSemaphore(device, &info, allocator, &fsd->RenderCompleteSemaphore);
			VK_CHECK(err);
		}
	}
}

void Renderer::createOrResizeWindow(VkInstance instance, VkPhysicalDevice physical_device, VkDevice device, WindowData* wd, uint32_t queue_family, const VkAllocationCallbacks* allocator, int width, int height, uint32_t min_image_count, VkImageUsageFlags image_usage) {
	//IM_ASSERT(g_FunctionsLoaded && "Need to call ImGui_ImplVulkan_LoadFunctions() if IMGUI_IMPL_VULKAN_NO_PROTOTYPES or VK_NO_PROTOTYPES are set!");
	assert(wd->Surface != VK_NULL_HANDLE);
	(instance);

	createWindowSwapChain(physical_device, device, wd, allocator, width, height, min_image_count, image_usage);
	createWindowCommandBuffers(physical_device, device, wd, queue_family, allocator);

	// FIXME: to submit the command buffer, we need a queue. In the examples folder, the ImGui_ImplVulkanH_CreateOrResizeWindow function is called
	// before the ImGui_ImplVulkan_Init function, so we don't have access to the queue yet. Here we have the queue_family that we can use to grab
	// a queue from the device and submit the command buffer. It would be better to have access to the queue as suggested in the FIXME below.
	VkCommandPool command_pool;
	VkCommandPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	pool_info.queueFamilyIndex = queue_family;
	VkResult err = vkCreateCommandPool(device, &pool_info, allocator, &command_pool);
	VK_CHECK(err);

	VkFenceCreateInfo fence_info = {};
	fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	VkFence fence;
	err = vkCreateFence(device, &fence_info, allocator, &fence);
	VK_CHECK(err);

	VkCommandBufferAllocateInfo alloc_info = {};
	alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	alloc_info.commandPool = command_pool;
	alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	alloc_info.commandBufferCount = 1;
	VkCommandBuffer command_buffer;
	err = vkAllocateCommandBuffers(device, &alloc_info, &command_buffer);
	VK_CHECK(err);

	VkCommandBufferBeginInfo begin_info = {};
	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	err = vkBeginCommandBuffer(command_buffer, &begin_info);
	VK_CHECK(err);

	// Transition the images to the correct layout for rendering
	for (uint32_t i = 0; i < wd->ImageCount; i++) {
		VkImageMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.image = wd->Frames[i].Backbuffer;
		barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.layerCount = 1;
		vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
	}

	err = vkEndCommandBuffer(command_buffer);
	VK_CHECK(err);
	VkSubmitInfo submit_info = {};
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &command_buffer;

	VkQueue queue;
	vkGetDeviceQueue(device, queue_family, 0, &queue);
	err = vkQueueSubmit(queue, 1, &submit_info, fence);
	VK_CHECK(err);
	err = vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
	VK_CHECK(err);
	err = vkResetFences(device, 1, &fence);
	VK_CHECK(err);

	err = vkResetCommandPool(device, command_pool, 0);
	VK_CHECK(err);

	// Destroy command buffer and fence and command pool
	vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
	vkDestroyCommandPool(device, command_pool, allocator);
	vkDestroyFence(device, fence, allocator);
	command_pool = VK_NULL_HANDLE;
	command_buffer = VK_NULL_HANDLE;
	fence = VK_NULL_HANDLE;
	queue = VK_NULL_HANDLE;
}

void Renderer::destroyFrame(VkDevice device, WindowFrame* fd, const VkAllocationCallbacks* allocator) {
	vkDestroyFence(device, fd->Fence, allocator);
	vkFreeCommandBuffers(device, fd->CommandPool, 1, &fd->CommandBuffer);
	vkDestroyCommandPool(device, fd->CommandPool, allocator);
	fd->Fence = VK_NULL_HANDLE;
	fd->CommandBuffer = VK_NULL_HANDLE;
	fd->CommandPool = VK_NULL_HANDLE;

	vkDestroyImageView(device, fd->BackbufferView, allocator);
	vkDestroyFramebuffer(device, fd->Framebuffer, allocator);
}

void Renderer::destroyFrameSemaphores(VkDevice device, WindowFrameSemaphores* fsd, const VkAllocationCallbacks* allocator) {
	vkDestroySemaphore(device, fsd->ImageAcquiredSemaphore, allocator);
	vkDestroySemaphore(device, fsd->RenderCompleteSemaphore, allocator);
	fsd->ImageAcquiredSemaphore = fsd->RenderCompleteSemaphore = VK_NULL_HANDLE;
}

int Renderer::run() {
	// Setup SDL
	// [If using SDL_MAIN_USE_CALLBACKS: all code below until the main loop starts would likely be your SDL_AppInit() function]
	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
		printf("Error: SDL_Init(): %s\n", SDL_GetError());
		return 1;
	}

	// Create window with Vulkan graphics context
	float main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
	SDL_WindowFlags window_flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
	SDL_Window* window = SDL_CreateWindow("Dear ImGui SDL3+Vulkan example", (int) (1280 * main_scale), (int) (800 * main_scale), window_flags);
	if (window == nullptr) {
		printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
		return 1;
	}

	std::vector<const char*> extensions;
	{
		uint32_t sdl_extensions_count = 0;
		const char* const* sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_extensions_count);
		for (uint32_t n = 0; n < sdl_extensions_count; n++)
			extensions.push_back(sdl_extensions[n]);
	}
	setupVulkan(extensions);

	// Create Window Surface
	VkSurfaceKHR surface;
	VkResult err;
	if (SDL_Vulkan_CreateSurface(window, g_Instance, g_Allocator, &surface) == 0) {
		printf("Failed to create Vulkan surface.\n");
		return 1;
	}

	// Create Framebuffers
	int w, h;
	SDL_GetWindowSizeInPixels(window, &w, &h);
	WindowData* wd = &g_MainWindowData;
	setupVulkanWindow(wd, surface, w, h);
	SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
	SDL_ShowWindow(window);

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void) io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	//ImGui::StyleColorsLight();

	// Setup scaling
	ImGuiStyle& style = ImGui::GetStyle();
	style.ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
	style.FontScaleDpi = main_scale;        // Set initial font scale. (in docking branch: using io.ConfigDpiScaleFonts=true automatically overrides this for every window depending on the current monitor)

	// Setup Platform/Renderer backends
	ImGui_ImplSDL3_InitForVulkan(window);
	ImGui_ImplVulkan_InitInfo init_info = {};
	//init_info.ApiVersion = VK_API_VERSION_1_3;              // Pass in your value of VkApplicationInfo::apiVersion, otherwise will default to header version.
	init_info.Instance = g_Instance;
	init_info.PhysicalDevice = g_PhysicalDevice;
	init_info.Device = g_Device;
	init_info.QueueFamily = g_QueueFamily;
	init_info.Queue = g_Queue;
	init_info.PipelineCache = g_PipelineCache;
	init_info.DescriptorPool = g_DescriptorPool;
	init_info.MinImageCount = g_MinImageCount;
	init_info.ImageCount = wd->ImageCount;
	init_info.Allocator = g_Allocator;
	init_info.PipelineInfoMain.RenderPass = wd->RenderPass;
	init_info.PipelineInfoMain.Subpass = 0;
	init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
	init_info.CheckVkResultFn = nullptr;
	ImGui_ImplVulkan_Init(&init_info);

	// Load Fonts
	// - If fonts are not explicitly loaded, Dear ImGui will select an embedded font: either AddFontDefaultVector() or AddFontDefaultBitmap().
	//   This selection is based on (style.FontSizeBase * style.FontScaleMain * style.FontScaleDpi) reaching a small threshold.
	// - You can load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
	// - If a file cannot be loaded, AddFont functions will return a nullptr. Please handle those errors in your code (e.g. use an assertion, display an error and quit).
	// - Read 'docs/FONTS.md' for more instructions and details.
	// - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use FreeType for higher quality font rendering.
	// - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
	//style.FontSizeBase = 20.0f;
	//io.Fonts->AddFontDefaultVector();
	//io.Fonts->AddFontDefaultBitmap();
	//io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf");
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf");
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf");
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf");
	//ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf");
	//IM_ASSERT(font != nullptr);

	// Our state
	bool show_demo_window = true;
	bool show_another_window = false;
	glm::vec4 clear_color = glm::vec4(0.45f, 0.55f, 0.60f, 1.00f);

	// Main loop
	bool done = false;
	while (!done) {
		// Poll and handle events (inputs, window resize, etc.)
		// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
		// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
		// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
		// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
		// [If using SDL_MAIN_USE_CALLBACKS: call ImGui_ImplSDL3_ProcessEvent() from your SDL_AppEvent() function]
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			ImGui_ImplSDL3_ProcessEvent(&event);
			if (event.type == SDL_EVENT_QUIT)
				done = true;
			if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window))
				done = true;
		}

		// [If using SDL_MAIN_USE_CALLBACKS: all code below would likely be your SDL_AppIterate() function]
		if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
			SDL_Delay(10);
			continue;
		}

		// Resize swap chain?
		int fb_width, fb_height;
		SDL_GetWindowSizeInPixels(window, &fb_width, &fb_height);
		if (fb_width > 0 && fb_height > 0 && (g_SwapChainRebuild || g_MainWindowData.Width != fb_width || g_MainWindowData.Height != fb_height)) {
			ImGui_ImplVulkan_SetMinImageCount(g_MinImageCount);
			createOrResizeWindow(g_Instance, g_PhysicalDevice, g_Device, wd, g_QueueFamily, g_Allocator, fb_width, fb_height, g_MinImageCount, 0);
			g_MainWindowData.FrameIndex = 0;
			g_SwapChainRebuild = false;
		}

		// Start the Dear ImGui frame
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplSDL3_NewFrame();
		ImGui::NewFrame();

		// 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
		if (show_demo_window)
			ImGui::ShowDemoWindow(&show_demo_window);

		// 2. Show a simple window that we create ourselves. We use a Begin/End pair to create a named window.
		{
			static float f = 0.0f;
			static int counter = 0;

			ImGui::Begin("Hello, world!");                          // Create a window called "Hello, world!" and append into it.

			ImGui::Text("This is some useful text.");               // Display some text (you can use a format strings too)
			ImGui::Checkbox("Demo Window", &show_demo_window);      // Edit bools storing our window open/close state
			ImGui::Checkbox("Another Window", &show_another_window);

			ImGui::SliderFloat("float", &f, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
			ImGui::ColorEdit3("clear color", (float*) &clear_color); // Edit 3 floats representing a color

			if (ImGui::Button("Button"))                            // Buttons return true when clicked (most widgets return true when edited/activated)
				counter++;
			ImGui::SameLine();
			ImGui::Text("counter = %d", counter);

			ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
			ImGui::End();
		}

		// 3. Show another simple window.
		if (show_another_window) {
			ImGui::Begin("Another Window", &show_another_window);   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
			ImGui::Text("Hello from another window!");
			if (ImGui::Button("Close Me"))
				show_another_window = false;
			ImGui::End();
		}

		// Rendering
		ImGui::Render();
		ImDrawData* draw_data = ImGui::GetDrawData();
		const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);
		if (!is_minimized) {
			wd->ClearValue.color.float32[0] = clear_color.x * clear_color.w;
			wd->ClearValue.color.float32[1] = clear_color.y * clear_color.w;
			wd->ClearValue.color.float32[2] = clear_color.z * clear_color.w;
			wd->ClearValue.color.float32[3] = clear_color.w;
			frameRender(wd, draw_data);
			framePresent(wd);
		}
	}

	// Cleanup
	// [If using SDL_MAIN_USE_CALLBACKS: all code below would likely be your SDL_AppQuit() function]
	err = vkDeviceWaitIdle(g_Device);
	VK_CHECK(err);
	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext();

	cleanupVulkanWindow(&g_MainWindowData);
	cleanupVulkan();

	SDL_DestroyWindow(window);
	SDL_Quit();

	return 0;
}