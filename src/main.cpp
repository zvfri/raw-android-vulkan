#include <android/log.h>
#include "androidnative/android_native_app_glue.h"
#include "vk.hpp"
#include "out.hpp"

Init init;
RenderData data;
VK vk;

enum class AppState {
    UNINITIALIZED,
    READY,
    WINDOW_LOST
};

AppState state = AppState::UNINITIALIZED;

static void destroy_surface_and_swapchain(struct android_app* app) {

    if (state == AppState::UNINITIALIZED) return;

    init.disp.deviceWaitIdle();
    vk.cleanup_swapchain(init, data, app);

    if (init.surface != VK_NULL_HANDLE) {
        vkb::destroy_surface(init.instance, init.surface);
        init.surface = VK_NULL_HANDLE;
    }

    state = AppState::WINDOW_LOST;
}

static bool create_surface_and_swapchain(struct android_app* app) {
    if (app->window == nullptr) return false;

    if (init.surface != VK_NULL_HANDLE) {
		vkb::destroy_surface(init.instance,init.surface);
        init.surface = VK_NULL_HANDLE;
    }

    init.surface = vk.create_surface(init.instance.instance, app);
    if (init.surface == VK_NULL_HANDLE) {
        LOG("Failed to create surface");
        return false;
    }

	init.device.surface = init.surface;

    if (vk.recreate_swapchain(init,data,app) != 0) {
        LOG("recreate_swapchain failed");
        return false;
    }

    return true;
}

void onAppCmd(struct android_app* app, int32_t cmd) {
    switch (cmd) {
        case APP_CMD_INIT_WINDOW: {
            LOG("APP_CMD_INIT_WINDOW");
            if (app->window == nullptr) break;

            if (state == AppState::UNINITIALIZED) {

				LOG("device initialization start");
                if (vk.device_initialization(init, app) != 0) break;
				LOG("create_swapchain start");
                if (vk.create_swapchain(init) != 0) break;
				LOG("depth_resource creation start");
				if (vk.create_depth_resources(init) != 0) break;
				LOG("get queues start");
                vk.get_queues(init, data);
				LOG("render pass start");
                vk.create_render_pass(init, data);
				LOG("graphics pipeline start");
                vk.create_graphics_pipeline(app, init, data);
				LOG(" upload mesh start");
				data.mesh = 
				vk.uploadMesh(init, data.indices,data.vertices);
				LOG(" create framebuffers start");
                vk.create_framebuffers(init, data);
				LOG("command pool start");
                vk.create_command_pool(init, data);
				LOG("command buffer start");
                vk.create_command_buffers(init, data);
				LOG("sync objects start");
                vk.create_sync_objects(init, data);
                state = AppState::READY;
                LOG("First-time Vulkan init done");
            } else if (state == AppState::WINDOW_LOST) {

                if (create_surface_and_swapchain(app)) {
                    state = AppState::READY;
                    LOG("Swapchain recreated on resume");
                }
            }
            break;
        }

		case APP_CMD_WINDOW_RESIZED: {
			LOG("APP_CMD_WINDOW_RESIZED");
			vk.recreate_swapchain(init,data,app);
			break;
		}

        case APP_CMD_TERM_WINDOW: {
            LOG("APP_CMD_TERM_WINDOW");
            destroy_surface_and_swapchain(app);
            break;
        }
    }
}

void android_main(struct android_app* app) {

	data.vertices = vertices;
	data.indices = indices;
	data.modelMatrix = glm::mat4(1.0f);

    app->onAppCmd = onAppCmd;

    while (true) {
        int events;
        struct android_poll_source* source;

        while (ALooper_pollOnce(state == AppState::READY ? 0 : -1, nullptr, &events, (void**)&source) >= 0) {
            if (source != nullptr) {
                source->process(app, source);
            }

            if (app->destroyRequested != 0) {
                if (state != AppState::UNINITIALIZED) {
                    init.disp.deviceWaitIdle();
                    vk.cleanup(init, data);
                }
                return;
            }
        }

        if (state == AppState::READY) {

            int res = vk.draw_frame(init, data, app);
            if (res != 0) {
                LOG("failed to draw frame"); }

        }
    }
}
