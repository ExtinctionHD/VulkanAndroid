#include "Image.h"
#include "StagingBuffer.h"
#include "utils.h"

Image::Image(
	Device *device,
	VkImageCreateFlags flags,
	VkFormat format,
	VkExtent3D extent,
	uint32_t mipLevels,
	uint32_t arrayLayers,
	VkSampleCountFlagBits sampleCount,
	VkImageUsageFlags usage,
	bool cubeMap) : swapChainImage(false)
{
	createThisImage(
		device,
		flags,
		format,
		extent,
		mipLevels,
		arrayLayers,
		sampleCount,
		usage,
		cubeMap);
}

Image::Image(Device *device, VkImage image, VkFormat format, VkExtent3D extent)
    : device(device),
      image(image),
      format(format),
      extent(extent),
      mipLevels(1),
      arrayLayers(1),
      sampleCount(VK_SAMPLE_COUNT_1_BIT),
      swapChainImage(true),
      cubeMap(false)
{

}

Image::~Image()
{
    for (auto view : views)
    {
        vkDestroyImageView(device->get(), view, nullptr);
    }
    if (!swapChainImage)
    {
        vkDestroyImage(device->get(), image, nullptr);
        vkFreeMemory(device->get(), memory, nullptr);
    }
}

VkImage Image::get() const
{
    return image;
}

VkImageView Image::getView(uint32_t index) const
{
    return views[index];
}

VkFormat Image::getFormat() const
{
    return format;
}

VkExtent3D Image::getExtent() const
{
	return extent;
}

VkSampleCountFlagBits Image::getSampleCount() const
{
	return sampleCount;
}

DescriptorInfo Image::getStorageImageInfo(uint32_t viewIndex) const
{
    DescriptorInfo info;
    info.image = VkDescriptorImageInfo{
        nullptr,
        views[viewIndex],
        VK_IMAGE_LAYOUT_GENERAL
    };

    return info;
}

void Image::pushView(VkImageViewType viewType, VkImageSubresourceRange subresourceRange)
{
    VkImageView view;

    VkImageViewCreateInfo createInfo{
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        nullptr,
        0,
        image,
        viewType,
        format,
        VkComponentMapping{},
        subresourceRange,
    };

    CALL_VK(vkCreateImageView(device->get(), &createInfo, nullptr, &view));

    views.push_back(view);
}

void Image::pushFullView(VkImageAspectFlags aspectFlags)
{
    const VkImageSubresourceRange subresourceRange{
        aspectFlags,
        0,
        mipLevels,
        0,
        arrayLayers
    };

    VkImageViewType viewType = arrayLayers == 1 ? VK_IMAGE_VIEW_TYPE_1D : VK_IMAGE_VIEW_TYPE_1D_ARRAY;
    if (extent.height > 0)
    {
        viewType = VK_IMAGE_VIEW_TYPE_2D;

        if (cubeMap)
        {
            LOGA(arrayLayers >= 6);
            viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        }
        else if (arrayLayers > 1)
        {
            viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        }
    }

    pushView(viewType, subresourceRange);
}

uint32_t Image::getMipLevelCount() const
{
	return mipLevels;
}

uint32_t Image::getArrayLayerCount() const
{
	return arrayLayers;
}

void Image::memoryBarrier(
    VkCommandBuffer commandBuffer,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkAccessFlags srcAccessMask,
    VkAccessFlags dstAccessMask,
    VkPipelineStageFlags srcStageMask,
    VkPipelineStageFlags dstStageMask,
    VkImageSubresourceRange subresourceRange)
{
    const VkImageMemoryBarrier barrier{
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        nullptr,
        srcAccessMask,
        dstAccessMask,
        oldLayout,
        newLayout,
        VK_QUEUE_FAMILY_IGNORED,
        VK_QUEUE_FAMILY_IGNORED,
        image,
        subresourceRange,
    };

    vkCmdPipelineBarrier(
        commandBuffer,
        srcStageMask, dstStageMask,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier);
}

void Image::transitLayout(
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkPipelineStageFlags srcStageMask,
    VkPipelineStageFlags dstStageMask,
    VkImageSubresourceRange subresourceRange)
{
	VkCommandBuffer commandBuffer = device->beginOneTimeCommands();

    VkAccessFlags srcAccessMask{};
    VkAccessFlags dstAccessMask{};

    // Source layouts (old)
    // Source access mask controls actions that have to be finished on the old layout
    // before it will be transitioned to the new layout
    switch (oldLayout)
    {
    case VK_IMAGE_LAYOUT_UNDEFINED:
        // Image layout is undefined (or does not matter)
        // Only valid as initial layout
        // No flags required, listed only for completeness
        srcAccessMask = 0;
        break;

    case VK_IMAGE_LAYOUT_PREINITIALIZED:
        // Image is preinitialized
        // Only valid as initial layout for linear images, preserves memory contents
        // Make sure host writes have been finished
        srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        break;

    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        // Image is a color attachment
        // Make sure any writes to the color buffer have been finished
        srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        break;

    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        // Image is a depth/stencil attachment
        // Make sure any writes to the depth/stencil buffer have been finished
        srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        break;

    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        // Image is a transfer source 
        // Make sure any reads from the image have been finished
        srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        break;

    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        // Image is a transfer destination
        // Make sure any writes to the image have been finished
        srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; 
        break;

    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        // Image is read by a shader
        // Make sure any shader reads from the image have been finished
        srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        break;
    default:
        // Other source layouts aren't handled (yet)
        break;
    }

    // Target layouts (new)
    // Destination access mask controls the dependency for the new image layout
    switch (newLayout)
    {
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        // Image will be used as a transfer destination
        // Make sure any writes to the image have been finished
        dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        break;

    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        // Image will be used as a transfer source
        // Make sure any reads from the image have been finished
        dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        break;

    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        // Image will be used as a color attachment
        // Make sure any writes to the color buffer have been finished
        dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
        break;

    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        // Image layout will be used as a depth/stencil attachment
        // Make sure any writes to depth/stencil buffer have been finished
        dstAccessMask = dstAccessMask | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        break;

    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        // Image will be read in a shader (sampler, input attachment)
        // Make sure any writes to the image have been finished
        if (oldLayout != VK_IMAGE_LAYOUT_UNDEFINED && !srcAccessMask)
        {
            srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        }
        dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        break;
    default:
        // Other source layouts aren't handled (yet)
        break;
    }

    memoryBarrier(
        commandBuffer,
        oldLayout,
        newLayout,
        srcAccessMask,
        dstAccessMask,
        srcStageMask,
        dstStageMask,
        subresourceRange);

	device->endOneTimeCommands(commandBuffer);
}

void Image::updateData(std::vector<const void*> data, uint32_t layersOffset, uint32_t pixelSize)
{
	const auto updatedLayers = uint32_t(data.size());

    LOGA(layersOffset + updatedLayers <= arrayLayers);

	const VkImageSubresourceRange subresourceRange{
		VK_IMAGE_ASPECT_COLOR_BIT,
		0,
		mipLevels,
		layersOffset,
		updatedLayers
	};
	const VkDeviceSize layerSize = extent.width * extent.height * pixelSize;

	StagingBuffer stagingBuffer(device, layerSize * updatedLayers);
	for (uint32_t i = 0; i < updatedLayers; i++)
	{
		stagingBuffer.updateData(data[i], i * layerSize,layerSize);
	}

	std::vector<VkBufferImageCopy> regions(updatedLayers);
	for (uint32_t i = 0; i < updatedLayers; i++)
	{
		regions[i] = VkBufferImageCopy{
			i * layerSize,
			0,
			0,
			{
				VK_IMAGE_ASPECT_COLOR_BIT,
				0,
				layersOffset + i,
				1
			},
			{ 0, 0, 0 },
			{ extent.width, extent.height, 1 }
		};
	}

	// before copying the layout of the image must be TRANSFER_DST
	transitLayout(
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
		subresourceRange);

	stagingBuffer.copyToImage(image, regions);
}

void Image::blitTo(
    VkCommandBuffer commandBuffer,
    Image *dstImage,
    VkImageSubresourceLayers srcSubresource,
    VkImageSubresourceLayers dstSubresource,
    std::array<VkOffset3D, 2> srcOffsets,
    std::array<VkOffset3D, 2> dstOffsets,
    VkFilter filter) const
{
    VkImageBlit region;

    region.srcSubresource = srcSubresource;
    region.srcOffsets[0] = srcOffsets[0];
    region.srcOffsets[1] = srcOffsets[1];

    region.dstSubresource = dstSubresource;
    region.dstOffsets[0] = dstOffsets[0];
    region.dstOffsets[1] = dstOffsets[1];

    vkCmdBlitImage(
        commandBuffer,
        image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        dstImage->image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region,
        filter);
}

void Image::blitTo(
    Image *dstImage,
    VkImageSubresourceLayers srcSubresource,
    VkImageSubresourceLayers dstSubresource,
    std::array<VkOffset3D, 2> srcOffsets,
    std::array<VkOffset3D, 2> dstOffsets,
    VkFilter filter) const
{
    VkCommandBuffer commandBuffer = device->beginOneTimeCommands();

    blitTo(commandBuffer, dstImage, srcSubresource, dstSubresource, srcOffsets, dstOffsets, filter);

    device->endOneTimeCommands(commandBuffer);
}

void Image::generateMipmaps(
    VkCommandBuffer commandBuffer,
    VkImageAspectFlags aspectFlags,
    VkFilter filter,
    VkImageLayout finalLayout,
    VkAccessFlags finalAccess,
    VkPipelineStageFlags finalStage)
{
    VkImageSubresourceRange subresourceRange{
        aspectFlags,
        0,
        1,
        0,
        arrayLayers
    };

    int32_t mipWidth = extent.width;
    int32_t mipHeight = extent.height;

    for (uint32_t i = 1; i < mipLevels; i++)
    {
        // transit current miplevel layout to TRANSFER_SRC
        subresourceRange.baseMipLevel = i - 1;
        memoryBarrier(
            commandBuffer,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            subresourceRange);

        const VkImageSubresourceLayers srcSubresource{
            aspectFlags,
            i - 1,
            0,
            arrayLayers,
        };

        const VkImageSubresourceLayers dstSubresource{
            aspectFlags,
            i,
            0,
            arrayLayers,
        };

        blitTo(
            commandBuffer,
            this,
            srcSubresource,
            dstSubresource,
            { {
                VkOffset3D{ 0, 0, 0 },
                VkOffset3D{ mipWidth, mipHeight, 1 }
            } },
            { {
                VkOffset3D{ 0, 0, 0 },
                VkOffset3D{ mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 }
            } },
            filter);

        memoryBarrier(
            commandBuffer,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            finalLayout,
            VK_ACCESS_TRANSFER_READ_BIT,
            finalAccess,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            finalStage,
            subresourceRange);

        // next miplevel scale
        if (mipWidth > 1) mipWidth /= 2;
        if (mipHeight > 1) mipHeight /= 2;
    }

    subresourceRange.baseMipLevel = mipLevels - 1;
    memoryBarrier(
        commandBuffer,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        finalLayout,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        finalAccess,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        finalStage,
        subresourceRange);
}

void Image::generateMipmaps(
    VkImageAspectFlags aspectFlags,
    VkFilter filter,
    VkImageLayout finalLayout,
    VkAccessFlags finalAccess,
    VkPipelineStageFlags finalStage)
{
    VkCommandBuffer commandBuffer = device->beginOneTimeCommands();

    generateMipmaps(commandBuffer, aspectFlags, filter, finalLayout, finalAccess, finalStage);

    device->endOneTimeCommands(commandBuffer);
}

void Image::copyTo(
    VkCommandBuffer commandBuffer,
    Image *dstImage,
    VkImageSubresourceLayers srcSubresource,
    VkImageSubresourceLayers dstSubresource,
    VkExtent3D extent) const
{
    VkImageCopy region{
        srcSubresource,
        { 0, 0, 0},
        dstSubresource,
        { 0, 0, 0 },
        extent
    };

    vkCmdCopyImage(
        commandBuffer,
        image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        dstImage->image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region);
}

void Image::copyTo(
    Image *dstImage,
    VkImageSubresourceLayers srcSubresource,
    VkImageSubresourceLayers dstSubresource,
    VkExtent3D extent) const
{
	VkCommandBuffer commandBuffer = device->beginOneTimeCommands();

    copyTo(commandBuffer, dstImage, srcSubresource, dstSubresource, extent);

	device->endOneTimeCommands(commandBuffer);
}

uint32_t Image::calculateMipLevelCount(VkExtent3D extent)
{
    const uint32_t mipLevels = math::ceilLog2(std::max(std::max(extent.width, extent.height), extent.depth));
    return mipLevels > 0 ? mipLevels : 1;
}

Image::Image() : swapChainImage(false)
{
}

void Image::createThisImage(
	Device* device,
	VkImageCreateFlags flags,
	VkFormat format,
	VkExtent3D extent,
	uint32_t mipLevels,
	uint32_t arrayLayers,
	VkSampleCountFlagBits sampleCount,
	VkImageUsageFlags usage,
	bool cubeMap)
{
	this->device = device;
    this->format = format;
	this->extent = extent;
	this->mipLevels = mipLevels;
	this->arrayLayers = arrayLayers;
    this->sampleCount = sampleCount;
    this->cubeMap = cubeMap;

	VkImageType imageType = VK_IMAGE_TYPE_1D;
	if (extent.height > 0)
	{
		imageType = VK_IMAGE_TYPE_2D;

		if (cubeMap)
		{
			LOGA(arrayLayers >= 6);

			flags = flags | VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
		}
	}

	VkImageCreateInfo imageInfo{
		VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		nullptr,
		flags,
		imageType,
		format,
		extent,
		mipLevels,
		arrayLayers,
		sampleCount,
		VK_IMAGE_TILING_OPTIMAL,
		usage,
		VK_SHARING_MODE_EXCLUSIVE,
		0,
		nullptr,
		VK_IMAGE_LAYOUT_UNDEFINED
	};

	CALL_VK(vkCreateImage(device->get(), &imageInfo, nullptr, &image));

	allocateMemory();

	vkBindImageMemory(device->get(), image, memory, 0);
}

void Image::allocateMemory()
{
	VkMemoryRequirements memRequirements;
	vkGetImageMemoryRequirements(device->get(), image, &memRequirements);

    const uint32_t memoryTypeIndex = device->findMemoryTypeIndex(
		memRequirements.memoryTypeBits,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VkMemoryAllocateInfo allocInfo{
		VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		nullptr,			
		memRequirements.size,	
		memoryTypeIndex,		
	};

    CALL_VK(vkAllocateMemory(device->get(), &allocInfo, nullptr, &memory));
}
