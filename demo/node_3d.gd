@tool
extends Node3D

func _ready() -> void:
	OIPComms.set_enable_log(true);
	pass # Replace with function body.

@export var register_tag_group := false: set = _register_tag_group 
func _register_tag_group(_value: bool) -> void:
	OIPComms.register_tag_group("test", 5000, "ab_eip", "localhost", "1,2", "ControlLogix")

@export var register_tag := false: set = _register_tag
func _register_tag(_value: bool) -> void:
	OIPComms.register_tag("test", "TEST_INPUT", 1)

@export var read_bit := false: set = _read_bit
func _read_bit(_value: bool) -> void:
	print(OIPComms.read_bit("test", "TEST_INPUT"))

@export var flip_bit := false: set = _flip_bit
func _flip_bit(_value: bool) -> void:
	#OIPComms.write_bit("test", "TEST_INPUT", not OIPComms.read_bit("test", "TEST_INPUT"))
	#print(OIPComms.read_bit("test", "TEST_INPUT"))
	OIPComms.write_bit("test", "TEST_INPUT", 0)
	OIPComms.write_bit("test", "TEST_INPUT", 1)
	OIPComms.write_bit("test", "TEST_INPUT", 0)
	OIPComms.write_bit("test", "TEST_INPUT", 1)

@export var enable_com := false: set = _test_editor
func _test_editor(value: bool) -> void:
	OIPComms.set_enable_comms(value)
	OIPComms.set_sim_running(value)
	enable_com = value
	pass
