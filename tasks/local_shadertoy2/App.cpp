#include "App.hpp"

#include <etna/RenderTargetStates.hpp>
#include <etna/Etna.hpp>
#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include "stbi_img.hpp"


App::App()
  : resolution{1280, 720}
  , useVsync{true}
{
  // First, we need to initialize Vulkan, which is not trivial because
  // extensions are required for just about anything.

  start_clock = std::clock();

  {
    // GLFW tells us which extensions it needs to present frames to the OS window.
    // Actually rendering anything to a screen is optional in Vulkan, you can
    // alternatively save rendered frames into files, send them over network, etc.
    // Instance extensions do not depend on the actual GPU, only on the OS.
    auto glfwInstExts = windowing.getRequiredVulkanInstanceExtensions();

    std::vector<const char*> instanceExtensions{glfwInstExts.begin(), glfwInstExts.end()};

    // We also need the swapchain device extension to get access to the OS
    // window from inside of Vulkan on the GPU.
    // Device extensions require HW support from the GPU.
    // Generally, in Vulkan, we call the GPU a "device" and the CPU/OS combination a "host."
    std::vector<const char*> deviceExtensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    // Etna does all of the Vulkan initialization heavy lifting.
    // You can skip figuring out how it works for now.
    etna::initialize(etna::InitParams{
      .applicationName = "Local Shadertoy",
      .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
      .instanceExtensions = instanceExtensions,
      .deviceExtensions = deviceExtensions,
      // Replace with an index if etna detects your preferred GPU incorrectly
      .physicalDeviceIndexOverride = {},
      .numFramesInFlight = 1,
    });
  }

  // Now we can create an OS window
  osWindow = windowing.createWindow(OsWindow::CreateInfo{
    .resolution = resolution,
  });

  // But we also need to hook the OS window up to Vulkan manually!
  {
    // First, we ask GLFW to provide a "surface" for the window,
    // which is an opaque description of the area where we can actually render.
    auto surface = osWindow->createVkSurface(etna::get_context().getInstance());

    // Then we pass it to Etna to do the complicated work for us
    vkWindow = etna::get_context().createWindow(etna::Window::CreateInfo{
      .surface = std::move(surface),
    });

    // And finally ask Etna to create the actual swapchain so that we can
    // get (different) images each frame to render stuff into.
    // Here, we do not support window resizing, so we only need to call this once.
    auto [w, h] = vkWindow->recreateSwapchain(etna::Window::DesiredProperties{
      .resolution = {resolution.x, resolution.y},
      .vsync = useVsync,
    });

    // Technically, Vulkan might fail to initialize a swapchain with the requested
    // resolution and pick a different one. This, however, does not occur on platforms
    // we support. Still, it's better to follow the "intended" path.
    resolution = {w, h};
  }

  // Next, we need a magical Etna helper to send commands to the GPU.
  // How it is actually performed is not trivial, but we can skip this for now.
  oneShotManager = etna::get_context().createOneShotCmdMgr();
  commandManager = etna::get_context().createPerFrameCmdMgr();

  samp = etna::Sampler(etna::Sampler::CreateInfo{
    .filter = vk::Filter::eLinear,
    .addressMode = vk::SamplerAddressMode::eRepeat,
    .name = "samp"
  });

  imgGen = etna::get_context().createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{100, 100, 1},
    .name = "generatedImage",
    .format = vk::Format::eB8G8R8A8Srgb,
    .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment,
  });

  int w, h, num;
  stbi_img texData(TEXTURES_ROOT "test_tex_1.png", &w, &h, &num, 4);
  etna::Image::CreateInfo fileInfo{
    .extent = vk::Extent3D{static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1},
    .name = "fileTexture",
    .format = vk::Format::eR8G8B8A8Srgb,
    .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
  };
  imgFile = etna::create_image_from_bytes(fileInfo, oneShotManager->start(), texData.data);

  etna::create_program("mainProgram", {LOCAL_SHADERTOY2_SHADERS_ROOT "v_shader.vert.spv", 
    LOCAL_SHADERTOY2_SHADERS_ROOT "toy.frag.spv"});
  etna::create_program("textureProgram", {LOCAL_SHADERTOY2_SHADERS_ROOT "v_shader.vert.spv", 
    LOCAL_SHADERTOY2_SHADERS_ROOT "texGen.frag.spv"});

  auto graphics_pipe = [](const char* name){
    return etna::get_context().getPipelineManager().createGraphicsPipeline(name,
      etna::GraphicsPipeline::CreateInfo {
        .fragmentShaderOutput = {
          .colorAttachmentFormats = {
            vk::Format::eB8G8R8A8Srgb
          }
        }
    });
  };

  mainPipe = graphics_pipe("mainProgram");
  texturePipe = graphics_pipe("textureProgram");
}

App::~App()
{
  ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());
}

void App::run()
{
  while (!osWindow->isBeingClosed())
  {
    windowing.poll();

    drawFrame();
  }

  // We need to wait for the GPU to execute the last frame before destroying
  // all resources and closing the application.
  ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());
}

void App::drawFrame()
{
  // First, get a command buffer to write GPU commands into.
  auto currentCmdBuf = commandManager->acquireNext();

  // Next, tell Etna that we are going to start processing the next frame.
  etna::begin_frame();

  // And now get the image we should be rendering the picture into.
  auto nextSwapchainImage = vkWindow->acquireNext();

  // When window is minimized, we can't render anything in Windows
  // because it kills the swapchain, so we skip frames in this case.
  if (nextSwapchainImage)
  {
    auto [backbuffer, backbufferView, backbufferAvailableSem] = *nextSwapchainImage;

    ETNA_CHECK_VK_RESULT(currentCmdBuf.begin(vk::CommandBufferBeginInfo{}));
    {
      // First of all, we need to "initialize" th "backbuffer", aka the current swapchain
      // image, into a state that is appropriate for us working with it. The initial state
      // is considered to be "undefined" (aka "I contain trash memory"), by the way.
      // "Transfer" in vulkanese means "copy or blit".
      // Note that Etna sometimes calls this for you to make life simpler, read Etna's code!
      etna::set_state(
        currentCmdBuf,
        imgGen.get(),
        // We are going to use the texture at the transfer stage...
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        // ...to transfer-write stuff into it...
        vk::AccessFlagBits2::eColorAttachmentWrite,
        // ...and want it to have the appropriate layout.
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageAspectFlagBits::eColor);
      // The set_state doesn't actually record any commands, they are deferred to
      // the moment you call flush_barriers.
      // As with set_state, Etna sometimes flushes on it's own.
      // Usually, flushes should be placed before "action", i.e. compute dispatches
      // and blit/copy operations.


      etna::flush_barriers(currentCmdBuf);
      
      {
        etna::RenderTargetState state{currentCmdBuf, {{}, {100, 100}}, {{imgGen.get(), imgGen.getView({})}}, {}};
        currentCmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, texturePipe.getVkPipeline());
        currentCmdBuf.draw(3, 1, 0, 0);
      }

      std::clock_t clk_now = std::clock();
      struct {
        glm::vec2 res;
        float duration;
      } pushConsts;
      pushConsts.duration = static_cast<float>(clk_now - start_clock) / CLOCKS_PER_SEC;
      pushConsts.res = resolution;
      currentCmdBuf.pushConstants(mainPipe.getVkPipelineLayout(), vk::ShaderStageFlagBits::eFragment,
        0, sizeof(pushConsts), &pushConsts);

      auto set_img_state = [currentCmdBuf](vk::Image img){
        etna::set_state(
          currentCmdBuf,
          img,
          vk::PipelineStageFlagBits2::eFragmentShader,
          vk::AccessFlagBits2::eColorAttachmentRead,
          vk::ImageLayout::eShaderReadOnlyOptimal,
          vk::ImageAspectFlagBits::eColor);
      };

      set_img_state(imgGen.get());
      set_img_state(imgFile.get());

      etna::set_state(
        currentCmdBuf,
        backbuffer,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eColorAttachmentWrite,
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageAspectFlagBits::eColor);
      etna::flush_barriers(currentCmdBuf);

      {
        etna::RenderTargetState state{currentCmdBuf, {{}, {resolution.x, resolution.y}},
          {{backbuffer, backbufferView}}, {}};
        auto s = etna::create_descriptor_set(
          etna::get_shader_program("mainProgram").getDescriptorLayoutId(0), currentCmdBuf, {
            etna::Binding{0, imgFile.genBinding(samp.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
            etna::Binding{1, imgGen.genBinding(samp.get(), vk::ImageLayout::eShaderReadOnlyOptimal)}
        });
        vk::DescriptorSet vs = s.getVkSet();
        currentCmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, mainPipe.getVkPipeline());
        currentCmdBuf.bindDescriptorSets(
          vk::PipelineBindPoint::eGraphics, mainPipe.getVkPipelineLayout(), 0, 1, &vs, 0, nullptr);

        currentCmdBuf.draw(3, 1, 0, 0);
      }

      // At the end of "rendering", we are required to change how the pixels of the
      // swpchain image are laid out in memory to something that is appropriate
      // for presenting to the window (while preserving the content of the pixels!).
      etna::set_state(
        currentCmdBuf,
        backbuffer,
        // This looks weird, but is correct. Ask about it later.
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        {},
        vk::ImageLayout::ePresentSrcKHR,
        vk::ImageAspectFlagBits::eColor);
      // And of course flush the layout transition.
      etna::flush_barriers(currentCmdBuf);
    }
    ETNA_CHECK_VK_RESULT(currentCmdBuf.end());

    // We are done recording GPU commands now and we can send them to be executed by the GPU.
    // Note that the GPU won't start executing our commands before the semaphore is
    // signalled, which will happen when the OS says that the next swapchain image is ready.
    auto renderingDone =
      commandManager->submit(std::move(currentCmdBuf), std::move(backbufferAvailableSem));

    // Finally, present the backbuffer the screen, but only after the GPU tells the OS
    // that it is done executing the command buffer via the renderingDone semaphore.
    const bool presented = vkWindow->present(std::move(renderingDone), backbufferView);

    if (!presented)
      nextSwapchainImage = std::nullopt;
  }

  etna::end_frame();

  // After a window us un-minimized, we need to restore the swapchain to continue rendering.
  if (!nextSwapchainImage && osWindow->getResolution() != glm::uvec2{0, 0})
  {
    auto [w, h] = vkWindow->recreateSwapchain(etna::Window::DesiredProperties{
      .resolution = {resolution.x, resolution.y},
      .vsync = useVsync,
    });
    ETNA_VERIFY((resolution == glm::uvec2{w, h}));
  }
}
