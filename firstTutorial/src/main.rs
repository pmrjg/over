use vulkano::command_buffer::allocator::StandardCommandBufferAllocator;
use vulkano::command_buffer::{AutoCommandBufferBuilder,
CommandBufferUsage, RenderPassBeginInfo, SubpassContents};

use vulkano::device::physical::PhysicalDeviceType;
use vulkano::device::{Device, DeviceCreateInfo, DeviceExtensions, QueueCreateInfo};
use vulkano::image::view::ImageView;
use vulkano::image::{ImageAccess, SwapchainImage};
use vulkano::instance::{Instance, InstanceCreateInfo};
use vulkano::pipeline::graphics::viewport::Viewport;
use vulkano::render_pass::{Framebuffer, FramebufferCreateInfo, RenderPass};
use vulkano::swapchain::{self, AcquireError, Swapchain, SwapchainCreateInfo, SwapchainCreationError, SwapchainPresentInfo,};
use vulkano::sync::{self, FlushError, GpuFuture};
use vulkano::{Version, VulkanLibrary};

use vulkano_win::VkSurfaceBuild;

use winit::event::{Event, WindowEvent};
use winit::event_loop::{ ControlFlow, EventLoop};
use winit::window::{Window, WindowBuilder};

use std::sync::Arc;



fn main() {
    println!("Hello, world!");
}
