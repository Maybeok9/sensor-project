#include <linux/module.h>
#define INCLUDE_VERMAGIC
#include <linux/build-salt.h>
#include <linux/elfnote-lto.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

BUILD_SALT;
BUILD_LTO_INFO;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif

static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0xdc658e53, "module_layout" },
	{ 0xcc3b02cf, "misc_deregister" },
	{ 0x48879f20, "hrtimer_init" },
	{ 0x92997ed8, "_printk" },
	{ 0x8c62e45, "misc_register" },
	{ 0xd36dc10c, "get_random_u32" },
	{ 0xf07464e4, "hrtimer_forward" },
	{ 0x65487097, "__x86_indirect_thunk_rax" },
	{ 0xb43f9365, "ktime_get" },
	{ 0x88db9f48, "__check_object_size" },
	{ 0x656e4a6e, "snprintf" },
	{ 0xd0da656b, "__stack_chk_fail" },
	{ 0x87a21cb3, "__ubsan_handle_out_of_bounds" },
	{ 0xd955afa6, "hrtimer_start_range_ns" },
	{ 0x4a666d56, "hrtimer_cancel" },
	{ 0x6b10bee1, "_copy_to_user" },
	{ 0xd35cce70, "_raw_spin_unlock_irqrestore" },
	{ 0x34db050b, "_raw_spin_lock_irqsave" },
	{ 0x5f8ff9eb, "kmem_cache_alloc_trace" },
	{ 0xc1f15dee, "kmalloc_caches" },
	{ 0x5b8239ca, "__x86_return_thunk" },
	{ 0x37a0cba, "kfree" },
	{ 0xbdfb6dbb, "__fentry__" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "98A34CE2E2B198AEE28D243");
