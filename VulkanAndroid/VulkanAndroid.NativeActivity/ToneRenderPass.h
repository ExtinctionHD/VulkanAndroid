#pragma once
#include "RenderPass.h"
#include "SwapChain.h"

class ToneRenderPass : public RenderPass
{
public:
    ToneRenderPass(Device *device, SwapChain *swapChain);

    uint32_t getColorAttachmentCount() const override;

    std::vector<VkClearValue> getClearValues() const override;

protected:
    void createAttachments() override;

    void createRenderPass() override;

    void createFramebuffers() override;

private:
    SwapChain *swapChain;
};

