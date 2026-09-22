#pragma once
#define LOG(...) __android_log_print(ANDROID_LOG_ERROR, "VULKAN_TEST",__VA_ARGS__)
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define VMA_IMPLEMENTATION
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_android.h>
#include <android/asset_manager.h>
#include <android/log.h>
#include <chrono>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>
#include "androidnative/android_native_app_glue.h"
#include "vkbootstrap/VkBootstrap.h"
#include "vkbootstrap/vk_mem_alloc.h"
#include "out.hpp"

const int MAX_FRAMES_IN_FLIGHT = 2;

struct AllocatedBuffer {
	VkBuffer buffer;
	VmaAllocation allocation;
	VmaAllocationInfo info;
};

struct AllocatedImage {
    VkImage image;
	VkImageView imageView;
	VkFormat format;
    VmaAllocation allocation;
};

struct GPUMeshBuffers {
	AllocatedBuffer indexBuffer;
	AllocatedBuffer vertexBuffer;
};

struct GPUDrawPushConstants {
	glm::mat4 worldMatrix;
};

struct Init {
    vkb::Instance instance;
	vkb::PhysicalDevice phys_device;
    vkb::InstanceDispatchTable inst_disp;
    VkSurfaceKHR surface;
    vkb::Device device;
    vkb::DispatchTable disp;
    vkb::Swapchain swapchain;
	VmaAllocator allocator;
};

struct RenderData {
    VkQueue graphics_queue;
    VkQueue present_queue;

    std::vector<VkImage> swapchain_images;
    std::vector<VkImageView> swapchain_image_views;
    std::vector<VkFramebuffer> framebuffers;

    VkRenderPass render_pass;
    VkPipelineLayout pipeline_layout;
    VkPipeline graphics_pipeline;

    VkCommandPool command_pool;
    std::vector<VkCommandBuffer> command_buffers;

    std::vector<VkSemaphore> available_semaphores;
    std::vector<VkSemaphore> finished_semaphore;
    std::vector<VkFence> in_flight_fences;
    std::vector<VkFence> image_in_flight;
    size_t current_frame = 0;

	GPUMeshBuffers mesh;
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	glm::mat4 modelMatrix;

};


class VK{

private :

	VkFence _immFence;
	VkCommandBuffer _immCommandBuffer;
	VkCommandPool _immCommandPool;
	VkQueue _graphicsQueue;
	uint32_t _graphicsQueueFamily;
	AllocatedImage depth;

	int create_image(Init& init, AllocatedImage& img,VkImageUsageFlags usage,VkExtent3D extent = {0, 0, 1},uint32_t mipLevels = 1) {

		if (extent.width == 0 || extent.height == 0) {
			extent.width = init.swapchain.extent.width;
			extent.height = init.swapchain.extent.height;}

		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.extent = extent;
		imageInfo.mipLevels = mipLevels;
		imageInfo.arrayLayers = 1;
		imageInfo.format = img.format;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.usage = usage;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

		if (vmaCreateImage(init.allocator, &imageInfo, &allocInfo, &img.image, &img.allocation, nullptr) != VK_SUCCESS) {
			LOG("failed to create image!");
			return -1;}

		return 0;
	}


	VkImageView create_imageView(Init& init, VkImage image, VkFormat format,VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT,uint32_t mipLevels = 1) {

		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = format;
		viewInfo.subresourceRange.aspectMask = aspectFlags;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = mipLevels;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;

		VkImageView imageView;
		if (vkCreateImageView(init.device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
			LOG("failed to create image view!"); }

		return imageView;
	}


	void transition_image(VkCommandBuffer cmd, VkImage image, VkFormat format,VkImageLayout oldLayout, VkImageLayout newLayout) {

		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = oldLayout;
		barrier.newLayout = newLayout;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;

		if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			if (format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT) barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;

		} else {
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; }

		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

		VkPipelineStageFlags srcStage;
		VkPipelineStageFlags dstStage;

		if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT; }

		else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;}

		else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                 VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT; }

		else {
			LOG("unsupported layout transition!");
			srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
			dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT; }

		vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0,
                          0, nullptr,
                          0, nullptr,
                          1, &barrier);
	}


	void destroy_image(Init& init,const AllocatedImage& img){

		vkDestroyImageView(init.device, img.imageView, nullptr);
		vmaDestroyImage(init.allocator, img.image, img.allocation);
	}


	VkFormat find_format(Init& init, const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {

		for (VkFormat format : candidates) {

			VkFormatProperties props;
			vkGetPhysicalDeviceFormatProperties(init.phys_device, format, &props);

			if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
            return format;
			} else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
            return format; }
		}

		LOG("failed to find supported format!");
		return VK_FORMAT_UNDEFINED;
	}


public :


	int device_initialization(Init& init,struct android_app* app) {

		vkb::InstanceBuilder instance_builder;
		auto instance_ret = instance_builder
			.set_app_name("myApp")
			.require_api_version(1, 0, 0)
			.build();

		if (!instance_ret) {
			LOG("Failed to create instance: %s", instance_ret.error().message().c_str());
			return -1;}

		init.instance = instance_ret.value();

		init.inst_disp = init.instance.make_table();

		init.surface = create_surface(init.instance.instance,app);

		if (init.surface == VK_NULL_HANDLE) {
			LOG("Failed to create surface!");
        return -1;}
    
		LOG("Surface created: %p", (void*)init.surface);

		vkb::PhysicalDeviceSelector phys_device_selector(init.instance);

		auto phys_device_ret = phys_device_selector
			.set_surface(init.surface)
			.select();

		if (!phys_device_ret) {
			LOG("Failed to select physical device: %s", phys_device_ret.error().message().c_str());
			return -1;}

		vkb::PhysicalDevice physical_device = phys_device_ret.value();

		init.phys_device = physical_device;

		vkb::DeviceBuilder device_builder{ physical_device };

		auto device_ret = device_builder.build();

		if (!device_ret) {
			LOG("Failed to create device: %s", device_ret.error().message().c_str());
        return -1;}

		init.device = device_ret.value();
		init.disp = init.device.make_table();

		auto graphics = init.device.get_queue_index(vkb::QueueType::graphics);
		auto present  = init.device.get_queue_index(vkb::QueueType::present);

		_graphicsQueue = init.device.get_queue(vkb::QueueType::graphics).value();
		_graphicsQueueFamily = graphics.value();

		create_allocator(init);
		init_immediate_submit(init);

		LOG("Graphics queue valid: %d, Present queue valid: %d", 
        graphics.has_value(), present.has_value());


		return 0;
	}


	std::vector<char> readAsset(AAssetManager* assetManager, const char* filename) {
    
		AAsset* asset = AAssetManager_open(assetManager, filename, AASSET_MODE_BUFFER); 
		if (!asset) {
        
			LOG("Failed to open asset: %s", filename); }
    
		size_t size = AAsset_getLength(asset);
		std::vector<char> buffer(size);

		AAsset_read(asset, buffer.data(), size);
		AAsset_close(asset);

		return buffer;
	}


	VkShaderModule createShaderModule(Init& init, const std::vector<char>& code) {

		VkShaderModuleCreateInfo create_info = {};
		create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		create_info.codeSize = code.size();
		create_info.pCode = reinterpret_cast<const uint32_t*>(code.data());

		VkShaderModule shaderModule;
		if (init.disp.createShaderModule(&create_info, nullptr, &shaderModule) != VK_SUCCESS) {
			return VK_NULL_HANDLE;}

		return shaderModule;
	}


	VkSurfaceKHR create_surface(VkInstance instance, struct android_app* app, VkAllocationCallbacks* allocator = nullptr) {

		if (app == nullptr || app->window == nullptr) {
        LOG("APP/WINDOW IS NULL");
        return VK_NULL_HANDLE;
		}

		VkSurfaceKHR surface = VK_NULL_HANDLE;
		VkAndroidSurfaceCreateInfoKHR createInfo = {};
		createInfo.sType  = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
		createInfo.pNext  = nullptr;
		createInfo.flags  = 0;
		createInfo.window = app->window;

		VkResult result = vkCreateAndroidSurfaceKHR(instance,&createInfo,allocator,&surface);

		if (result != VK_SUCCESS) {
			LOG("SURFACE CREATION FAILED");
			surface = VK_NULL_HANDLE;
		} 
		return surface;
	}


	int get_queues(Init& init, RenderData& data) {
		auto gq = init.device.get_queue(vkb::QueueType::graphics);
		if (!gq.has_value()) {
			LOG("failed to get graphics queue: %s", gq.error().message().c_str());
			return -1;}
		data.graphics_queue = gq.value();

		auto pq = init.device.get_queue(vkb::QueueType::present);
		if (!pq.has_value()) {
			LOG("failed to get present queue: %s ", pq.error().message().c_str());
			return -1;}
		data.present_queue = pq.value();
		return 0;
	}


	int create_render_pass(Init& init, RenderData& data) {

		VkAttachmentDescription color_attachment = {};
		color_attachment.format = init.swapchain.image_format;
		color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
		color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		VkAttachmentReference color_attachment_ref = {};
		color_attachment_ref.attachment = 0;
		color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;


		VkAttachmentDescription depth_attachment = {};
		depth_attachment.format = depth.format;
		depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
		depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference depth_attachment_ref = {};
		depth_attachment_ref.attachment = 1;
		depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass = {};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &color_attachment_ref;
		subpass.pDepthStencilAttachment = &depth_attachment_ref;

		VkSubpassDependency dependency = {};
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;
		dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | 
									VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT ;
		dependency.srcAccessMask = 0;
		dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
									VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT ;

		dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | 
									VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT ;

		std::array<VkAttachmentDescription, 2> attachments = { color_attachment, depth_attachment };

		VkRenderPassCreateInfo render_pass_info = {};
		render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		render_pass_info.attachmentCount = static_cast<uint32_t>(attachments.size());
		render_pass_info.pAttachments = attachments.data();
		render_pass_info.subpassCount = 1;
		render_pass_info.pSubpasses = &subpass;
		render_pass_info.dependencyCount = 1;
		render_pass_info.pDependencies = &dependency;

		if (init.disp.createRenderPass(&render_pass_info, nullptr, &data.render_pass) != VK_SUCCESS) {
        LOG("failed to create render pass");
        return -1;

		}

		return 0;
	}


	int create_graphics_pipeline(struct android_app* app,Init& init, RenderData& data) {

		AAssetManager* assetManager = app->activity->assetManager;
		auto vert_code = readAsset(assetManager,"shaders/cube.vert.spv");
		auto frag_code = readAsset(assetManager,"shaders/cube.frag.spv");

		VkShaderModule vert_module = createShaderModule(init, vert_code);
		VkShaderModule frag_module = createShaderModule(init, frag_code);

		if (vert_module == VK_NULL_HANDLE || frag_module == VK_NULL_HANDLE) {
			LOG("failed to create shader module");
			return -1;}

		VkPipelineShaderStageCreateInfo vert_stage_info = {};
		vert_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		vert_stage_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
		vert_stage_info.module = vert_module;
		vert_stage_info.pName = "main";

		VkPipelineShaderStageCreateInfo frag_stage_info = {};
		frag_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		frag_stage_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		frag_stage_info.module = frag_module;
		frag_stage_info.pName = "main";

		VkPipelineShaderStageCreateInfo shader_stages[] = { vert_stage_info, frag_stage_info };

		VkVertexInputBindingDescription bindingDescription{};
		bindingDescription.binding = 0;
		bindingDescription.stride = sizeof(Vertex);
		bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		std::array<VkVertexInputAttributeDescription, 5> attributeDescriptions{};
		attributeDescriptions[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position) };
		attributeDescriptions[1] = { 1, 0, VK_FORMAT_R32_SFLOAT,       offsetof(Vertex, uv_x) };
		attributeDescriptions[2] = { 2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal) };
		attributeDescriptions[3] = { 3, 0, VK_FORMAT_R32_SFLOAT,       offsetof(Vertex, uv_y) };
		attributeDescriptions[4] = { 4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, color) };

		VkPipelineVertexInputStateCreateInfo vertex_input_info = {};
		vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertex_input_info.vertexBindingDescriptionCount = 1;
		vertex_input_info.pVertexBindingDescriptions = &bindingDescription;
		vertex_input_info.vertexAttributeDescriptionCount = attributeDescriptions.size();
		vertex_input_info.pVertexAttributeDescriptions = attributeDescriptions.data();

		VkPipelineInputAssemblyStateCreateInfo input_assembly = {};
		input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		input_assembly.primitiveRestartEnable = VK_FALSE;

		VkViewport viewport = {};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = (float)init.swapchain.extent.width;
		viewport.height = (float)init.swapchain.extent.height;
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;

		VkRect2D scissor = {};
		scissor.offset = { 0, 0 };
		scissor.extent = init.swapchain.extent;

		VkPipelineViewportStateCreateInfo viewport_state = {};
		viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewport_state.viewportCount = 1;
		viewport_state.pViewports = nullptr;
		viewport_state.scissorCount = 1;
		viewport_state.pScissors = nullptr;

		VkPipelineRasterizationStateCreateInfo rasterizer = {};
		rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizer.depthClampEnable = VK_FALSE;
		rasterizer.rasterizerDiscardEnable = VK_FALSE;
		rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
		rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterizer.depthBiasEnable = VK_FALSE;

		VkPipelineMultisampleStateCreateInfo multisampling = {};
		multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampling.sampleShadingEnable = VK_FALSE;
		multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
		colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		colorBlendAttachment.blendEnable = VK_FALSE;

		VkPipelineColorBlendStateCreateInfo color_blending = {};
		color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		color_blending.logicOpEnable = VK_FALSE;
		color_blending.logicOp = VK_LOGIC_OP_COPY;
		color_blending.attachmentCount = 1;
		color_blending.pAttachments = &colorBlendAttachment;
		color_blending.blendConstants[0] = 0.0f;
		color_blending.blendConstants[1] = 0.0f;
		color_blending.blendConstants[2] = 0.0f;
		color_blending.blendConstants[3] = 0.0f;

		VkPushConstantRange pushConstantRange = {};
		pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		pushConstantRange.offset = 0;
		pushConstantRange.size = sizeof(GPUDrawPushConstants);

		VkPipelineLayoutCreateInfo pipeline_layout_info = {};
		pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipeline_layout_info.setLayoutCount = 0;
		pipeline_layout_info.pushConstantRangeCount = 1;
		pipeline_layout_info.pPushConstantRanges = &pushConstantRange;

		if (init.disp.createPipelineLayout(&pipeline_layout_info, nullptr, &data.pipeline_layout) != VK_SUCCESS) {
			LOG("failed to create pipeline layout");
			return -1;}

		std::vector<VkDynamicState> dynamic_states = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

		VkPipelineDynamicStateCreateInfo dynamic_info = {};
		dynamic_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamic_info.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
		dynamic_info.pDynamicStates = dynamic_states.data();

		VkPipelineDepthStencilStateCreateInfo depthStencil{};
		depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depthStencil.depthTestEnable = VK_TRUE;
		depthStencil.depthWriteEnable = VK_TRUE;
		depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
		depthStencil.depthBoundsTestEnable = VK_FALSE;
		depthStencil.stencilTestEnable = VK_FALSE;
		depthStencil.minDepthBounds = 0.0f;
		depthStencil.maxDepthBounds = 1.0f;

		VkGraphicsPipelineCreateInfo pipeline_info = {};
		pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipeline_info.stageCount = 2;
		pipeline_info.pStages = shader_stages;
		pipeline_info.pVertexInputState = &vertex_input_info;
		pipeline_info.pInputAssemblyState = &input_assembly;
		pipeline_info.pViewportState = &viewport_state;
		pipeline_info.pRasterizationState = &rasterizer;
		pipeline_info.pMultisampleState = &multisampling;
		pipeline_info.pColorBlendState = &color_blending;
		pipeline_info.pDynamicState = &dynamic_info;
		pipeline_info.layout = data.pipeline_layout;
		pipeline_info.renderPass = data.render_pass;
		pipeline_info.subpass = 0;
		pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
		pipeline_info.pDepthStencilState = &depthStencil;

		if (init.disp.createGraphicsPipelines(VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &data.graphics_pipeline) != VK_SUCCESS) {
			LOG("failed to create pipline");
			return -1;}

		init.disp.destroyShaderModule(frag_module, nullptr);
		init.disp.destroyShaderModule(vert_module, nullptr);
		return 0;

	}


	int create_depth_resources(Init& init) {
    
		depth.format = find_format(init,{VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},VK_IMAGE_TILING_OPTIMAL,VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);

		if(create_image(init,depth,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0) { LOG("failed to create depth image . see createDepthResources!"); }
		depth.imageView = create_imageView(init, depth.image, depth.format);

		return 0;
	}


	int create_framebuffers(Init& init, RenderData& data) {

		data.swapchain_images = init.swapchain.get_images().value();
		data.swapchain_image_views = init.swapchain.get_image_views().value();
		data.framebuffers.resize(data.swapchain_image_views.size());

		for (size_t i = 0; i < data.swapchain_image_views.size(); i++) {
			std::array<VkImageView, 2> attachments = { data.swapchain_image_views[i],depth.imageView};

        VkFramebufferCreateInfo framebuffer_info = {};
        framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass = data.render_pass;
        framebuffer_info.attachmentCount = 2;
        framebuffer_info.pAttachments = attachments.data();
        framebuffer_info.width = init.swapchain.extent.width;
        framebuffer_info.height = init.swapchain.extent.height;
        framebuffer_info.layers = 1;

        if (init.disp.createFramebuffer(&framebuffer_info, nullptr, &data.framebuffers[i]) != VK_SUCCESS) return -1;
    
		}
		return 0;
	}


	int create_command_pool(Init& init, RenderData& data) {

		VkCommandPoolCreateInfo pool_info = {};
		pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		pool_info.queueFamilyIndex = init.device.get_queue_index(vkb::QueueType::graphics).value();

		if (init.disp.createCommandPool(&pool_info, nullptr, &data.command_pool) != VK_SUCCESS) {
        LOG("failed to create command pool");
        return -1;

		} return 0;
	}


	int create_command_buffers(Init& init, RenderData& data) {
		
		data.command_buffers.resize(data.framebuffers.size());
		VkCommandBufferAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool = data.command_pool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = (uint32_t)data.command_buffers.size();

		if (init.disp.allocateCommandBuffers(&allocInfo, data.command_buffers.data()) != VK_SUCCESS) {
			LOG("command_buffer allocation failed");
			return -1;}

		 return 0;
	}



	void record_command_buffer(Init& init, RenderData& data,uint32_t imageIndex) {

		VkCommandBuffer cmd = data.command_buffers[imageIndex];

		init.disp.resetCommandBuffer(cmd, 0);

		VkCommandBufferBeginInfo beginInfo = {};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		init.disp.beginCommandBuffer(cmd, &beginInfo);

		VkRenderPassBeginInfo rpInfo = {};
		rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rpInfo.renderPass = data.render_pass;
		rpInfo.framebuffer = data.framebuffers[imageIndex];
		rpInfo.renderArea.extent = init.swapchain.extent;
		rpInfo.renderArea.offset = {0, 0};
		std::array<VkClearValue, 2> clearValues{};
		clearValues[0].color = {{0.f, 0.f, 0.f, 1.f}};
		clearValues[1].depthStencil = {1.0f, 0};

		rpInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
		rpInfo.pClearValues = clearValues.data();

		init.disp.cmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
		init.disp.cmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, data.graphics_pipeline);

		VkViewport viewport{0,0,(float)init.swapchain.extent.width,(float)init.swapchain.extent.height,0.f,1.f};
		VkRect2D scissor{{0,0}, init.swapchain.extent};
		init.disp.cmdSetViewport(cmd, 0, 1, &viewport);
		init.disp.cmdSetScissor(cmd, 0, 1, &scissor);



		GPUDrawPushConstants pushConstants;

		float aspect = (float)init.swapchain.extent.width/(float)init.swapchain.extent.height;

		static auto startTime = std::chrono::high_resolution_clock::now();
		float time = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - startTime).count();

		data.modelMatrix = glm::mat4(1.0f);
		pushConstants.worldMatrix = glm::mat4(1.0f);
		glm::mat4 proj = glm::perspective(glm::radians(70.0f), aspect,0.1f, 10000.0f);

		proj[1][1] *= -1;
		glm::mat4 view = glm::translate(glm::mat4(1.0f),glm::vec3{0,0,-5});
		glm::mat4 rotate = glm::rotate(data.modelMatrix, glm::radians(45.0f), glm::vec3(0,0,1));
		glm::mat4 spin = glm::rotate(data.modelMatrix, time * glm::radians(90.0f), glm::vec3(0,1,0));
		pushConstants.worldMatrix = proj * view  * rotate * spin;

		init.disp.cmdPushConstants(cmd, data.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pushConstants), &pushConstants);



		VkDeviceSize offset = 0;
		init.disp.cmdBindVertexBuffers(cmd, 0, 1, &data.mesh.vertexBuffer.buffer, &offset);
		init.disp.cmdBindIndexBuffer(cmd, data.mesh.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
		init.disp.cmdDrawIndexed(cmd, static_cast<uint32_t>(data.indices.size()), 1, 0, 0, 0);

		init.disp.cmdEndRenderPass(cmd);
		init.disp.endCommandBuffer(cmd);

	}


	int create_sync_objects(Init& init, RenderData& data) {

		data.available_semaphores.resize(MAX_FRAMES_IN_FLIGHT);
		data.finished_semaphore.resize(init.swapchain.image_count);
		data.in_flight_fences.resize(MAX_FRAMES_IN_FLIGHT);
		data.image_in_flight.resize(init.swapchain.image_count, VK_NULL_HANDLE);

		VkSemaphoreCreateInfo semaphore_info = {};
		semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		VkFenceCreateInfo fence_info = {};
		fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

		for (size_t i = 0; i < init.swapchain.image_count; i++) {
        if (init.disp.createSemaphore(&semaphore_info, nullptr, &data.finished_semaphore[i]) != VK_SUCCESS) {
            LOG("failed to create sync objects");
            return -1;}
		}

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (init.disp.createSemaphore(&semaphore_info, nullptr, &data.available_semaphores[i]) != VK_SUCCESS || init.disp.createFence(&fence_info, nullptr, &data.in_flight_fences[i]) != VK_SUCCESS) {
            LOG("failed to create sync objects");
            return -1;}
		}
    
		return 0;
	}


    int create_swapchain(Init& init) {

		vkb::SwapchainBuilder swapchain_builder{ init.device };
		auto swap_ret = swapchain_builder
            .set_old_swapchain(init.swapchain)
			.set_pre_transform_flags(VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
            .build();
        if (!swap_ret) {
            LOG("error at create_swapchain : %s , %d",swap_ret.error().message().c_str(),swap_ret.vk_result());
        return -1;}                                                                                                                                             vkb::destroy_swapchain(init.swapchain);                                     init.swapchain = swap_ret.value();                                                                                                                      return 0;
    }


	int cleanup_swapchain(Init& init, RenderData& data,struct android_app* app) {

        for (auto framebuffer : data.framebuffers) {
            init.disp.destroyFramebuffer(framebuffer, nullptr);}
            data.framebuffers.clear();

        for (auto view : data.swapchain_image_views) {
            init.disp.destroyImageView(view, nullptr);}
            data.swapchain_image_views.clear();

		vkb::destroy_swapchain(init.swapchain);
        init.swapchain = {};

		return 0;
	}


	int recreate_swapchain(Init& init, RenderData& data,struct android_app* app) {

		init.disp.deviceWaitIdle();

		cleanup_swapchain(init,data,app);

		if (0 != create_swapchain(init)) return -1;
		if (0 != create_framebuffers(init, data)) return -1;
		if (0 != create_command_buffers(init, data)) return -1;

		return 0;
	}


	int draw_frame(Init& init, RenderData& data,struct android_app* app) {

	//	LOG("===== draw_frame is running =====");

		init.disp.waitForFences(1, &data.in_flight_fences[data.current_frame], VK_TRUE, UINT64_MAX);

		uint32_t image_index = 0;
		VkResult result = init.disp.acquireNextImageKHR(init.swapchain, UINT64_MAX, data.available_semaphores[data.current_frame], VK_NULL_HANDLE, &image_index);

	//	LOG("acquireNextImageKHR result = %d, imageIndex = %u", result, image_index);
		if (result == VK_ERROR_OUT_OF_DATE_KHR) {
			return recreate_swapchain(init,data,app);}
		else if (result == VK_TIMEOUT || result == VK_NOT_READY) {
			return 0;}
		else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
			LOG("FAILED TO ACQUIRE SWAPCHAIN IMAGE, %d",result);
			return -1;}

		if (data.image_in_flight[image_index] != VK_NULL_HANDLE) {
        init.disp.waitForFences(1, &data.image_in_flight[image_index], VK_TRUE, UINT64_MAX);}
		data.image_in_flight[image_index] = data.in_flight_fences[data.current_frame];

		record_command_buffer(init, data, image_index);

		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

		VkSemaphore wait_semaphores[] = { data.available_semaphores[data.current_frame] };
		VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = wait_semaphores;
		submitInfo.pWaitDstStageMask = wait_stages;

		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &data.command_buffers[image_index];

		VkSemaphore signal_semaphores[] = { data.finished_semaphore[image_index] };
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = signal_semaphores;

		init.disp.resetFences(1, &data.in_flight_fences[data.current_frame]);

		VkResult submitResult = init.disp.queueSubmit(data.graphics_queue, 1, &submitInfo, data.in_flight_fences[data.current_frame]);

		if (submitResult != VK_SUCCESS) {
			LOG("FAILED TO SUBMIT DRAW COMMAND BUFFER,VkResult= %d",submitResult);
			return -1;}

		VkPresentInfoKHR present_info = {};
		present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

		present_info.waitSemaphoreCount = 1;
		present_info.pWaitSemaphores = signal_semaphores;

		VkSwapchainKHR swapChains[] = { init.swapchain };
		present_info.swapchainCount = 1;
		present_info.pSwapchains = swapChains;

		present_info.pImageIndices = &image_index;

		result = init.disp.queuePresentKHR(data.present_queue, &present_info);

		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
			return recreate_swapchain(init, data,app);
		} else if (result != VK_SUCCESS) {
			__android_log_print(ANDROID_LOG_ERROR, "VULKAN_TEST", "FAILED TO PRESENT SWAPCHAIN IMAGE");
			return -1; }

		data.current_frame = (data.current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
		return 0; 
	}


	void cleanup(Init& init, RenderData& data) {

		for (size_t i = 0; i < init.swapchain.image_count; i++) {
			init.disp.destroySemaphore(data.finished_semaphore[i], nullptr);}
		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
			init.disp.destroySemaphore(data.available_semaphores[i], nullptr);
			init.disp.destroyFence(data.in_flight_fences[i], nullptr);}

		init.disp.destroyCommandPool(data.command_pool, nullptr);

		for (auto framebuffer : data.framebuffers) {
			init.disp.destroyFramebuffer(framebuffer, nullptr);}

		init.disp.destroyPipeline(data.graphics_pipeline, nullptr);
		init.disp.destroyPipelineLayout(data.pipeline_layout, nullptr);
		init.disp.destroyRenderPass(data.render_pass, nullptr);

		init.swapchain.destroy_image_views(data.swapchain_image_views);
		destroy_image(init,depth);
		vkb::destroy_swapchain(init.swapchain);
		vkb::destroy_device(init.device);
		vkb::destroy_surface(init.instance, init.surface);
		vkb::destroy_instance(init.instance);
		vkDestroyCommandPool(init.device, _immCommandPool, nullptr);
		vkDestroyFence(init.device, _immFence, nullptr);
	}


	void init_immediate_submit(Init& init) {

		VkCommandPoolCreateInfo commandPoolInfo = {};
		commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		commandPoolInfo.queueFamilyIndex = _graphicsQueueFamily;

		vkCreateCommandPool(init.device, &commandPoolInfo, nullptr, &_immCommandPool);

		VkCommandBufferAllocateInfo cmdAllocInfo = {};
		cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		cmdAllocInfo.commandPool = _immCommandPool;
		cmdAllocInfo.commandBufferCount = 1;
		cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

		vkAllocateCommandBuffers(init.device, &cmdAllocInfo, &_immCommandBuffer);

		VkFenceCreateInfo fenceCreateInfo = {};
		fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

		vkCreateFence(init.device, &fenceCreateInfo, nullptr, &_immFence);

	}


	void immediate_submit(Init& init,std::function<void(VkCommandBuffer cmd)>&& function){

		init.disp.resetFences(1, &_immFence);
		init.disp.resetCommandBuffer(_immCommandBuffer, 0);

		VkCommandBuffer cmd = _immCommandBuffer;

		VkCommandBufferBeginInfo cmdBeginInfo = {};
		cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		init.disp.beginCommandBuffer(cmd, &cmdBeginInfo);

		function(cmd);

		init.disp.endCommandBuffer(cmd);

		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &cmd;

		init.disp.queueSubmit(_graphicsQueue, 1, &submitInfo, _immFence);

		init.disp.waitForFences(1,&_immFence,true,9999999999);
	}


	void create_allocator(Init& init) {
	
		VmaVulkanFunctions vulkanFunctions = {};
		vulkanFunctions.vkGetInstanceProcAddr = init.instance.fp_vkGetInstanceProcAddr;
		vulkanFunctions.vkGetDeviceProcAddr =
        (PFN_vkGetDeviceProcAddr)init.instance.fp_vkGetInstanceProcAddr(init.instance, "vkGetDeviceProcAddr");

		LOG("GetInstanceProcAddr = %p, GetDeviceProcAddr = %p",
        (void*)vulkanFunctions.vkGetInstanceProcAddr, (void*)vulkanFunctions.vkGetDeviceProcAddr);

		VmaAllocatorCreateInfo allocatorInfo = {};
		allocatorInfo.physicalDevice = init.phys_device;
		allocatorInfo.device = init.device;
		allocatorInfo.instance = init.instance;
		allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_0;
		allocatorInfo.pVulkanFunctions = &vulkanFunctions;
		vmaCreateAllocator(&allocatorInfo, &init.allocator);

	}


	AllocatedBuffer create_buffer(Init& init,size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage) {

		VkBufferCreateInfo bufferInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
		bufferInfo.pNext = nullptr;
		bufferInfo.size = allocSize;

		bufferInfo.usage = usage;

		VmaAllocationCreateInfo vmaallocInfo = {};
		vmaallocInfo.usage = memoryUsage;
		vmaallocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
		AllocatedBuffer newBuffer;

		vmaCreateBuffer(init.allocator, &bufferInfo, &vmaallocInfo, &newBuffer.buffer,&newBuffer.allocation,&newBuffer.info);

		return newBuffer;

	}


	void destroy_buffer(Init& init,const AllocatedBuffer& buffer)

	{ vmaDestroyBuffer(init.allocator, buffer.buffer, buffer.allocation); }


	GPUMeshBuffers uploadMesh(Init& init,std::vector<uint32_t> indices, std::vector<Vertex> vertices)
{
		const size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
		const size_t indexBufferSize = indices.size() * sizeof(uint32_t);

		GPUMeshBuffers Mesh;

		Mesh.vertexBuffer = create_buffer(init, vertexBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT , VMA_MEMORY_USAGE_GPU_ONLY);

		Mesh.indexBuffer = create_buffer(init, indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,VMA_MEMORY_USAGE_GPU_ONLY);

		AllocatedBuffer staging = create_buffer(init, vertexBufferSize + indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

		void* data;
		vmaMapMemory(init.allocator, staging.allocation, &data);
		memcpy(data, vertices.data(), vertexBufferSize);
		memcpy((char*)data + vertexBufferSize, indices.data(), indexBufferSize);
		vmaUnmapMemory(init.allocator, staging.allocation);

		immediate_submit(init,[&](VkCommandBuffer cmd) {
			VkBufferCopy vertexCopy{ 0 };
			vertexCopy.dstOffset = 0;
			vertexCopy.srcOffset = 0;
			vertexCopy.size = vertexBufferSize;

			vkCmdCopyBuffer(cmd, staging.buffer, Mesh.vertexBuffer.buffer, 1, &vertexCopy);

			VkBufferCopy indexCopy{ 0 };
			indexCopy.dstOffset = 0;
			indexCopy.srcOffset = vertexBufferSize;
			indexCopy.size = indexBufferSize;

			vkCmdCopyBuffer(cmd, staging.buffer, Mesh.indexBuffer.buffer, 1, &indexCopy);
	});

		destroy_buffer(init, staging);

		return Mesh;
	}


	AllocatedImage createTexture(Init& init,void* data, VkFormat format, VkImageUsageFlags usage, VkExtent3D size, uint32_t mipLevels) {

		size_t data_size = size.depth * size.width * size.height * 4;
		AllocatedBuffer uploadbuffer = create_buffer(init, data_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

		memcpy(uploadbuffer.info.pMappedData, data, data_size);

		AllocatedImage new_image;
		new_image.format = format;
		
		create_image(init, new_image, usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, size, mipLevels );

		immediate_submit(init,[&](VkCommandBuffer cmd) {
			transition_image(cmd, new_image.image, new_image.format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

			VkBufferImageCopy copyRegion = {};
			copyRegion.bufferOffset = 0;
			copyRegion.bufferRowLength = 0;
			copyRegion.bufferImageHeight = 0;

			copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			copyRegion.imageSubresource.mipLevel = 0;
			copyRegion.imageSubresource.baseArrayLayer = 0;
			copyRegion.imageSubresource.layerCount = 1;
			copyRegion.imageExtent = size;

			vkCmdCopyBufferToImage(cmd, uploadbuffer.buffer,new_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

			transition_image(cmd, new_image.image, new_image.format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		});

		destroy_buffer(init,uploadbuffer);

		return new_image;
	}

};

