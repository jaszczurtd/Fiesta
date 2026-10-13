#ifndef VP37_POWER_STAGE_H
#define VP37_POWER_STAGE_H

/**
 * @file vp37_power_stage.h
 * @brief The VP37 hardware the module reads from the device_tree.json of the
 * firmware being built: the "vp37" node (type fiesta,vp37-power-stage) and the
 * "vp37ScanAux" input that shares the current scan (ECU sensor multiplexer,
 * bench demand pot). The ECU and the pump bench wire their own pins; the
 * parts and values come from the type, through the JH_HW_NODE_VP37_* macros.
 * Both actuator drives are active low: a command is written as
 * VP37_PWM_RESOLUTION minus the command. Drive settings and control tuning
 * are in vp37_config.h, the drive frequency in vp37_drive_config.h because
 * the Adjustometer build reads it too.
 */

#if !defined(JH_HW_NODE_VP37_PRESENT) || (JH_HW_NODE_VP37_PRESENT != 1)
#error                                                                         \
    "the VP37 module needs the vp37 node (fiesta,vp37-power-stage) of device_tree.json"
#endif

#if !defined(JH_HW_NODE_VP37_SCAN_AUX_PRESENT) ||                              \
    (JH_HW_NODE_VP37_SCAN_AUX_PRESENT != 1)
#error "the VP37 current scan needs the vp37ScanAux input of device_tree.json"
#endif

#endif
