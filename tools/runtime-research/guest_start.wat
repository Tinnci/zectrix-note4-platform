;; Test code that runs before the caller can meter an exported callback.
(module
  (import "note4_probe" "emit" (func (param i32 i32) (result i32)))
  (memory 1 1)
  (func $initialize (loop $forever (br $forever)))
  (start $initialize)
  (func (export "step") (result i32) (i32.const 0)))
