@tool
extends Node3D

@export var register_tag_group := false: set = _register_tag_group 
func _register_tag_group(_value: bool) -> void:
	# AB EIP example
	OIPComms.register_tag_group("test", 5000, "ab_eip", "localhost", "1,2", "ControlLogix")
	# Modbus TCP example (Schneider w/ word-swapped byte order)
	#OIPComms.register_tag_group("modbus_test", 1000, "modbus_tcp", "192.168.1.200", "1", "", "1032")

@export var register_tag := false: set = _register_tag
func _register_tag(_value: bool) -> void:
	OIPComms.register_tag("test", "TEST_INPUT", 1)
	# Modbus TCP: holding register 519, elem_count=2 for float32
	#OIPComms.register_tag("modbus_test", "hr519", 2)

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

@export var test_editor := false: set = _test_editor
func _test_editor(value: bool) -> void:
	OIPComms.set_enable_comms(value)
	test_editor = value
	pass
