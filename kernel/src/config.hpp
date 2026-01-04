#ifndef CONFIG_HPP
#define CONFIG_HPP 1

/*
	Configurations

	Those are the recommended and default configurations
*/

/*
    Configure whether the schedule uses the same PML4 as the kernel or not
*/
#define SCHED_CFG_USE_SAME_PML4
// #undef SCHED_CFG_USE_SAME_PML4

/*
	Initial (ps2k) keyboard driver buffer size
*/
#define PS2K_CFG_INITIAL_BUF_SIZE 512

/*
	Configure whether the (ps2k) keyboard driver prints key events (DBG ONLY!)
*/
// #define PS2K_CFG_DEBUG
#undef PS2K_CFG_DEBUG

/*
	Configure whether the (ps2k) keyboard driver bufer is allocated via malloc or valloc (valloc better for big buf size)
*/
#define PS2K_CFG_ALLOC_BUF_MALLOC
// #undef PS2K_CFG_ALLOC_BUF_MALLOC

/*
	Configure whether the (ps2k) keyboard driver buffer is allocated or not
*/
#define PS2K_CFG_ALLOC_BUF
// #undef PS2K_CFG_ALLOC_BUF 

/*
	Configure whether the (ps2k) keyboard driver drops events on failure or buffer overflow
*/
// #define PS2K_CFG_DROP_EVENTS_ON_FAILURE_OR_OVERFLOW
#undef PS2K_CFG_DROP_EVENTS_ON_FAILURE_OR_OVERFLOW

/*
	Configure whether uACPI driver is verbose or not
*/
#define ACPI_CFG_VERBOSE
// #undef ACPI_CFG_VERBOSE

/*
	Configure whether the PCI driver is verbose or not
*/
// #define PCI_CFG_VERBOSE
#undef PCI_CFG_VERBOSE

/*
	Configure whether the VMM prints errors
*/
#define VMM_ERR_VERBOSE
// #undef VMM_ERR_VERBOSE

/*
	Configure nested exception threshold
*/
#define EXCEPTIONS_CFG_NESTING_THRESHOLD 3

/*
	Configure whether the debugger runs
*/
#define EXCEPTIONS_CFG_RUN_DEBUGGER
// #undef EXCEPTIONS_CFG_RUN_DEBUGGER

#endif
