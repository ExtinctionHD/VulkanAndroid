#include "Engine.h"
#include "Vertex.h"
#include "utils.h"
#include "Position.h"
#include "GraphicsPipeline.h"
#include "ComputePipeline.h"

Engine::Engine() : created(false), outdated(false)
{
	InitVulkan();

	instance = new Instance();
}

Engine::~Engine()
{
    destroy();

	delete instance;
}

bool Engine::create(ANativeWindow *window)
{
    if (created) return true;

    surface = new Surface(instance->get(), window);
    device = new Device(instance->get(), surface->get(), instance->getLayers());
    swapChain = new SwapChain(device, surface->get(), window::getExtent(window));
    scene = new Scene(device, swapChain->getExtent());

    mainRenderPass = new MainRenderPass(device, swapChain->getExtent(), VK_SAMPLE_COUNT_1_BIT);
    mainRenderPass->create();

    descriptorPool = new DescriptorPool(
        device,
        {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, Scene::TEXTURE_COUNT + 1 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 + swapChain->getImageCount() },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, Scene::BUFFER_COUNT + 0 }
        },
        DESCRIPTOR_TYPE_COUNT + 1 + swapChain->getImageCount());

    initDescriptorSets();
    initPipelines();

    renderingFinished = createSemaphore();
    computingFinished = createSemaphore();
    imageAvailable = createSemaphore();

    initRenderingCommands();
    initComputingCommands();

    LOGI("Engine created.");

    return created = true;
}

bool Engine::recreate(ANativeWindow *window)
{
    if (!created) return false;

    if (outdated)
    {
        vkDeviceWaitIdle(device->get());

        const auto extent = window::getExtent(window);

        delete surface;

        surface = new Surface(instance->get(), window);
        device->updateSurface(surface->get());
        swapChain->recreate(surface->get(), extent);

        mainRenderPass->recreate(extent);

        for(auto pipeline: pipelines)
        {
            pipeline->recreate();
        }

        scene->resize(extent);

        initRenderingCommands();

        outdated = false;
    }

    return true;
}

void Engine::outdate()
{
    outdated = true;
}

void Engine::pause()
{
    paused = true;
}

void Engine::unpause()
{
    paused = false;
    scene->skipTime();
}

bool Engine::onPause()
{
    return paused;
}

void Engine::handleMotion(glm::vec2 delta)
{
    scene->handleMotion(delta);
}

bool Engine::drawFrame()
{
    if (!created || outdated || paused) return false;

    scene->update();

    uint32_t imageIndex;

    VkResult result = vkAcquireNextImageKHR(
        device->get(), 
        swapChain->get(), 
        UINT64_MAX, 
        imageAvailable, 
        nullptr, 
        &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        LOGE("vkAcquireNextImageKHR: VK_ERROR_OUT_OF_DATE_KHR.");
        return false;
    }
    if (result != VK_SUBOPTIMAL_KHR)
    {
        CALL_VK(result);
    }

    std::vector<VkSemaphore> renderingSignalSemaphores{ renderingFinished };
    VkSubmitInfo renderingSubmitInfo{
        VK_STRUCTURE_TYPE_SUBMIT_INFO,
        nullptr,
        0,
        nullptr,
        nullptr,
        1,
        &renderingCommands,
        uint32_t(renderingSignalSemaphores.size()),
        renderingSignalSemaphores.data(),
    };
    CALL_VK(vkQueueSubmit(device->getGraphicsQueue(), 1, &renderingSubmitInfo, nullptr));

    std::vector<VkSemaphore> computingWaitSemaphores{ renderingFinished, imageAvailable};
    std::vector<VkPipelineStageFlags> computingWaitStages{ VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT };
    std::vector<VkSemaphore> computingSignalSemaphores{ computingFinished };
    VkSubmitInfo submitInfo{
        VK_STRUCTURE_TYPE_SUBMIT_INFO,
        nullptr,
        uint32_t(computingWaitSemaphores.size()),
        computingWaitSemaphores.data(),
        computingWaitStages.data(),
        1,
        &computingCommands[imageIndex],
        uint32_t(computingSignalSemaphores.size()),
        computingSignalSemaphores.data(),
    };
    CALL_VK(vkQueueSubmit(device->getGraphicsQueue(), 1, &submitInfo, nullptr));

    std::vector<VkSwapchainKHR> swapChains{ swapChain->get() };
    VkPresentInfoKHR presentInfo{
        VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        nullptr,
        uint32_t(computingSignalSemaphores.size()),
        computingSignalSemaphores.data(),
        uint32_t(swapChains.size()),
        swapChains.data(),
        &imageIndex,
        nullptr,
    };
    result = vkQueuePresentKHR(device->getPresentQueue(), &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        LOGI("Presentation error.");
        return false;
    }

    CALL_VK(result);

    return true;
}

bool Engine::destroy()
{
    if (!created) return true;

    vkDeviceWaitIdle(device->get());

    vkDestroySemaphore(device->get(), computingFinished, nullptr);
    vkDestroySemaphore(device->get(), renderingFinished, nullptr);
    vkDestroySemaphore(device->get(), imageAvailable, nullptr);

    for (auto pipeline : pipelines)
    {
        delete pipeline;
    }

    for (auto descriptor : descriptors)
    {
        delete descriptor;
    }

    delete mainRenderPass;
    delete descriptorPool;
    delete scene;
    delete swapChain;
    delete device;
    delete surface;

    created = false;

    LOGI("Engine destroyed.");

    return true;
}

void Engine::initDescriptorSets()
{

    descriptors.resize(DESCRIPTOR_TYPE_COUNT);

    // Scene:

    descriptors[DESCRIPTOR_TYPE_SCENE] = new DescriptorSets(
        descriptorPool,
        { { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, { VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT } } });
    descriptors[DESCRIPTOR_TYPE_SCENE]->pushDescriptorSet(
        { { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, { scene->getCameraBuffer(), scene->getLightingBuffer() } } });

    // Earth:

    std::vector<IDescriptorSource*> earthTextures;
    for (const auto texture : scene->getEarthTextures())
    {
        earthTextures.push_back(texture);
    }

    descriptors[DESCRIPTOR_TYPE_EARTH] = new DescriptorSets(
        descriptorPool,
        {
            {
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                std::vector<VkShaderStageFlags>(earthTextures.size(), VK_SHADER_STAGE_FRAGMENT_BIT)
            },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, { VK_SHADER_STAGE_VERTEX_BIT } },
        });
    descriptors[DESCRIPTOR_TYPE_EARTH]->pushDescriptorSet(
        {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, earthTextures },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, { scene->getEarthTransformationBuffer() } }
        });

    // Clouds and skybox:

    descriptors[DESCRIPTOR_TYPE_CLOUDS_AND_SKYBOX] = new DescriptorSets(
        descriptorPool,
        {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, { VK_SHADER_STAGE_FRAGMENT_BIT } },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, { VK_SHADER_STAGE_VERTEX_BIT } }
        });
    descriptors[DESCRIPTOR_TYPE_CLOUDS_AND_SKYBOX]->pushDescriptorSet(
        {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, { scene->getCloudsTexture() } },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, { scene->getCloudsTransformationBuffer() } }
        });
    descriptors[DESCRIPTOR_TYPE_CLOUDS_AND_SKYBOX]->pushDescriptorSet(
        {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, { scene->getSkyboxTexture() } },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, { scene->getSkyboxTransformationBuffer() } }
        });

    // Tone:

    descriptors[DESCRIPTOR_TYPE_TONE_SRC] = new DescriptorSets(
        descriptorPool,
        { { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, { VK_SHADER_STAGE_COMPUTE_BIT } } });
    descriptors[DESCRIPTOR_TYPE_TONE_SRC]->pushDescriptorSet(
        { { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, { mainRenderPass->getColorImage() } } });


    descriptors[DESCRIPTOR_TYPE_TONE_DST] = new DescriptorSets(
        descriptorPool,
        { { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, { VK_SHADER_STAGE_COMPUTE_BIT } } });

    for(const auto swapChainImage : swapChain->getImages())
    {
        descriptors[DESCRIPTOR_TYPE_TONE_DST]->pushDescriptorSet(
            { { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, { swapChainImage } } });
    }
}

void Engine::initPipelines()
{
    pipelines.resize(PIPELINE_TYPE_COUNT);

    // Earth:

    std::string shadersPath = "shaders/Earth/";
    std::vector<std::shared_ptr<ShaderModule>> shaders{
        std::make_shared<ShaderModule>(device, shadersPath + "vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
        std::make_shared<ShaderModule>(device, shadersPath + "frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
    };
    pipelines[PIPELINE_TYPE_EARTH] = new GraphicsPipeline(
        device,
        mainRenderPass,
        {
            descriptors[DESCRIPTOR_TYPE_SCENE]->getLayout(),
            descriptors[DESCRIPTOR_TYPE_EARTH]->getLayout()
        },
        {},
        shaders,
        { Vertex::getBindingDescription(0) },
        Vertex::getAttributeDescriptions(0, 0),
        true);

    // Clouds:

    shadersPath = "shaders/Clouds/";
    shaders = {
        std::make_shared<ShaderModule>(device, shadersPath + "vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
        std::make_shared<ShaderModule>(device, shadersPath + "frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
    };
    pipelines[PIPELINE_TYPE_CLOUDS] = new GraphicsPipeline(
        device,
        mainRenderPass,
        {
            descriptors[DESCRIPTOR_TYPE_SCENE]->getLayout(),
            descriptors[DESCRIPTOR_TYPE_CLOUDS_AND_SKYBOX]->getLayout()
        },
        {},
        shaders,
        { Vertex::getBindingDescription(0) },
        Vertex::getAttributeDescriptions(0, 0),
        true);

    // Skybox:

    shadersPath = "shaders/Skybox/";
    shaders = {
        std::make_shared<ShaderModule>(device, shadersPath + "vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
        std::make_shared<ShaderModule>(device, shadersPath + "frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
    };
    pipelines[PIPELINE_TYPE_SKYBOX] = new GraphicsPipeline(
        device,
        mainRenderPass,
        {
            descriptors[DESCRIPTOR_TYPE_SCENE]->getLayout(),
            descriptors[DESCRIPTOR_TYPE_CLOUDS_AND_SKYBOX]->getLayout()
        },
        {},
        shaders,
        { Position::getBindingDescription(0) },
        Position::getAttributeDescriptions(0, 0),
        true);

    // Tone:

    pipelines[PIPELINE_TYPE_TONE] = new ComputePipeline(
        device,
        { 
            descriptors[DESCRIPTOR_TYPE_TONE_SRC]->getLayout(), 
            descriptors[DESCRIPTOR_TYPE_TONE_DST]->getLayout()
        },
        {},
        std::make_shared<ShaderModule>(device, "shaders/Tone/comp.spv", VK_SHADER_STAGE_COMPUTE_BIT));
}

VkSemaphore Engine::createSemaphore() const
{
    VkSemaphore semaphore;

    VkSemaphoreCreateInfo createInfo{
        VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        nullptr,
        0,
    };

    CALL_VK(vkCreateSemaphore(device->get(), &createInfo, nullptr, &semaphore));

    return semaphore;
}

void Engine::initRenderingCommands()
{
    const VkCommandPool commandPool = device->getCommandPool();

    if (renderingCommands)
    {
        vkFreeCommandBuffers(device->get(), commandPool, 1, &renderingCommands);
    }

    VkCommandBufferAllocateInfo allocInfo{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        nullptr,
        commandPool,
        VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        1,
    };

    CALL_VK(vkAllocateCommandBuffers(device->get(), &allocInfo, &renderingCommands));

    VkCommandBufferBeginInfo beginInfo{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        nullptr,
        VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
        nullptr,
    };

    CALL_VK(vkBeginCommandBuffer(renderingCommands, &beginInfo));

    {
        const VkRect2D renderArea{
        { 0, 0 },
        mainRenderPass->getExtent()
        };
        auto clearValues = mainRenderPass->getClearValues();
        VkRenderPassBeginInfo mainRenderPassBeginInfo{
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            nullptr,
            mainRenderPass->get(),
            mainRenderPass->getFramebuffers().front(),
            renderArea,
            uint32_t(clearValues.size()),
            clearValues.data()
        };

        vkCmdBeginRenderPass(renderingCommands, &mainRenderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

        // Skybox:

        vkCmdBindPipeline(renderingCommands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines[PIPELINE_TYPE_SKYBOX]->get());
        std::vector<VkDescriptorSet> descriptorSets{
            descriptors[DESCRIPTOR_TYPE_SCENE]->getDescriptorSet(0),
            descriptors[DESCRIPTOR_TYPE_CLOUDS_AND_SKYBOX]->getDescriptorSet(1)
        };
        vkCmdBindDescriptorSets(
            renderingCommands,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelines[PIPELINE_TYPE_SKYBOX]->getLayout(),
            0,
            descriptorSets.size(),
            descriptorSets.data(),
            0,
            nullptr);
        scene->drawCube(renderingCommands);

        // Earth:

        vkCmdBindPipeline(renderingCommands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines[PIPELINE_TYPE_EARTH]->get());
        descriptorSets = {
            descriptors[DESCRIPTOR_TYPE_SCENE]->getDescriptorSet(0),
            descriptors[DESCRIPTOR_TYPE_EARTH]->getDescriptorSet(0)
        };
        vkCmdBindDescriptorSets(
            renderingCommands,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelines[PIPELINE_TYPE_EARTH]->getLayout(),
            0,
            descriptorSets.size(),
            descriptorSets.data(),
            0,
            nullptr);
        scene->drawSphere(renderingCommands);

        // Clouds:

        vkCmdBindPipeline(renderingCommands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines[PIPELINE_TYPE_CLOUDS]->get());
        descriptorSets = {
            descriptors[DESCRIPTOR_TYPE_SCENE]->getDescriptorSet(0),
            descriptors[DESCRIPTOR_TYPE_CLOUDS_AND_SKYBOX]->getDescriptorSet(0)
        };
        vkCmdBindDescriptorSets(
            renderingCommands,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelines[PIPELINE_TYPE_CLOUDS]->getLayout(),
            0,
            descriptorSets.size(),
            descriptorSets.data(),
            0,
            nullptr);
        scene->drawSphere(renderingCommands);

        vkCmdEndRenderPass(renderingCommands);
    }

    CALL_VK(vkEndCommandBuffer(renderingCommands));

    LOGI("Rendering commands created.");
}

void Engine::initComputingCommands()
{
    const VkCommandPool commandPool = device->getCommandPool();
    const uint32_t count = swapChain->getImageCount();

    if (!computingCommands.empty())
    {
        vkFreeCommandBuffers(device->get(), commandPool, uint32_t(computingCommands.size()), computingCommands.data());
    }

    computingCommands.resize(count);

    VkCommandBufferAllocateInfo allocInfo{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        nullptr,
        commandPool,
        VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        count,
    };

    CALL_VK(vkAllocateCommandBuffers(device->get(), &allocInfo, computingCommands.data()));

    for (uint32_t i = 0; i < count; i++)
    {

        VkCommandBufferBeginInfo beginInfo{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            nullptr,
            VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
            nullptr,
        };

        CALL_VK(vkBeginCommandBuffer(computingCommands[i], &beginInfo));

        {
            vkCmdBindPipeline(computingCommands[i], VK_PIPELINE_BIND_POINT_COMPUTE, pipelines[PIPELINE_TYPE_TONE]->get());
            std::vector<VkDescriptorSet> descriptorSets{
                descriptors[DESCRIPTOR_TYPE_TONE_SRC]->getDescriptorSet(0),
                descriptors[DESCRIPTOR_TYPE_TONE_DST]->getDescriptorSet(i)
            };
            vkCmdBindDescriptorSets(
                computingCommands[i],
                VK_PIPELINE_BIND_POINT_COMPUTE,
                pipelines[PIPELINE_TYPE_TONE]->getLayout(),
                0,
                descriptorSets.size(),
                descriptorSets.data(),
                0,
                nullptr);

            const VkImageSubresourceRange subresourceRange{
                VK_IMAGE_ASPECT_COLOR_BIT,
                0,
                1,
                0,
                1
            };

            const VkImage image = swapChain->getImages()[i]->get();

            const VkImageMemoryBarrier beginningBarrier{
                VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                nullptr,
                0,
                VK_ACCESS_SHADER_WRITE_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_QUEUE_FAMILY_IGNORED,
                VK_QUEUE_FAMILY_IGNORED,
                image,
                subresourceRange
            };

            vkCmdPipelineBarrier(
                computingCommands[i],
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0,
                0, nullptr,
                0, nullptr,
                1, &beginningBarrier);

            const VkExtent2D imageExtent = swapChain->getExtent();

            vkCmdDispatch(computingCommands[i], imageExtent.width / 8, imageExtent.height / 8, 1);

            const VkImageMemoryBarrier endingBarrier{
                VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                nullptr,
                VK_ACCESS_SHADER_WRITE_BIT,
                VK_ACCESS_UNIFORM_READ_BIT,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                VK_QUEUE_FAMILY_IGNORED,
                VK_QUEUE_FAMILY_IGNORED,
                image,
                subresourceRange
            };

            vkCmdPipelineBarrier(
                computingCommands[i], 
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                0,
                0, nullptr,
                0, nullptr,
                1, &endingBarrier);
        }

        CALL_VK(vkEndCommandBuffer(computingCommands[i]));
    }

    LOGI("Computing commands created.");
}
