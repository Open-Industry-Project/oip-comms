@tool
extends Node3D


# Called when the node enters the scene tree for the first time.
func _ready() -> void:
	OIPComms.set_enable_log(true);
	pass # Replace with function body.

@export var register_tag_group := false: set = _register_tag_group 
func _register_tag_group(_value: bool) -> void:
	OIPComms.register_tag_group("testS7", 0, "s7", "10.80.1.2", "", "S7_1200")

@export var register_tags := false: set = _register_tag
func _register_tag(_value: bool) -> void:
	OIPComms.register_tag("testS7", "MB0", 1)
	OIPComms.register_tag("testS7", "M0.0", 1)
	OIPComms.register_tag("testS7", "IW25", 1)
	OIPComms.register_tag("testS7", "QD2", 1)

@export var read := false: set = _read
func _read(_value: bool) -> void:
	print(OIPComms.read_bit("testS7", "M0.0"))
	print(OIPComms.read_uint8("testS7", "MB0"))

@export var write_MB0_to_0 := false: set = _bit_zero
func _bit_zero(_value: bool) -> void:
	OIPComms.write_uint8("testS7", "MB0", 0)

@export var write_MB0_to_255 := false: set = _bit_one
func _bit_one(_value: bool) -> void:
	OIPComms.write_uint8("testS7", "MB0", 255)

@export var enable_com := false: set = _test_editor
func _test_editor(value: bool) -> void:
	OIPComms.set_enable_comms(value)
	OIPComms.set_sim_running(value)
	enable_com = value
	pass
