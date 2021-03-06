#pragma once

class SurfaceSupportDetails
{
public:
	SurfaceSupportDetails(VkPhysicalDevice device, VkSurfaceKHR surface);

	VkSurfaceCapabilitiesKHR getCapabilities() const;

	std::vector<VkSurfaceFormatKHR> getFormats() const;

	std::vector<VkPresentModeKHR> getPresentModes() const;

	bool suitable() const;

private:
	VkSurfaceCapabilitiesKHR capabilities;

	std::vector<VkSurfaceFormatKHR> formats;

	std::vector<VkPresentModeKHR> presentModes;

};

