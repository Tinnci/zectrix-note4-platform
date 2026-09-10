;; WAMR also invokes this legacy export automatically during instantiation.
(module
  (import "note4_probe" "emit" (func (param i32 i32) (result i32)))
  (memory 1 1)
  (func (export "__post_instantiate") (loop $forever (br $forever)))
  (func (export "step") (result i32) (i32.const 0)))
