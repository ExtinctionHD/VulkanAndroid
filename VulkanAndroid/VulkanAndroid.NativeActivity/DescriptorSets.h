#pragma once
#include "DescriptorPool.h"

class DescriptorSets
{
public:
    DescriptorSets(DescriptorPool *pool, DescriptorShaderStages descriptorShaderStages);

    ~DescriptorSets();

    VkDescriptorSetLayout getLayout() const;

    VkDescriptorSet getDescriptorSet(uint32_t index) const;

    void pushDescriptorSet(DescriptorInfos descriptorInfos);

    void updateDescriptorSet(uint32_t index, DescriptorInfos descriptorInfos);

    void removeDescriptorSets(uint32_t index, uint32_t count);

private:
    DescriptorPool *pool;

    VkDescriptorSetLayout layout;

    std::vector<VkDescriptorSet> sets;
};

