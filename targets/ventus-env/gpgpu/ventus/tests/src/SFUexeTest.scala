package play

import chisel3._
import chisel3.util._
import chiseltest._
import org.scalatest.freespec.AnyFreeSpec
import pipeline._
import top.parameters._

class SFUexeTest extends AnyFreeSpec with ChiselScalatestTester {
  "SFUexe should handle 0 / NaN correctly" in {
    test(new SFUexe) { c =>
      // Prepare input data
      // We want to test 0 / NaN
      // a = 0
      // b = 0xfffffc0c (NaN)
      // We need to set up vExeData
      
      // Initialize inputs
      c.io.in.valid.poke(false.B)
      c.io.out_v.ready.poke(true.B)
      c.io.out_x.ready.poke(true.B)
      c.io.rm.poke(0.U)

      c.clock.step()

      // Set up input
      c.io.in.valid.poke(true.B)
      
      // Set up ctrl
      // We need to set isvec=1, fp=1, wvd=1, alu_fn=FN_FDIV
      // FN_FDIV = 0
      // But wait, alu_fn for SFU is different?
      // In DecodeUnit:
      // VFDIV_VF -> FN_FDIV (0)
      // In SFUexe:
      // floatDiv(i).in.bits.op := i_ctrl.alu_fn(2, 0)
      // So op=0 is Div.
      
      c.io.in.bits.ctrl.isvec.poke(true.B)
      c.io.in.bits.ctrl.fp.poke(true.B)
      c.io.in.bits.ctrl.wvd.poke(true.B)
      c.io.in.bits.ctrl.alu_fn.poke(0.U) // FN_FDIV
      c.io.in.bits.ctrl.wid.poke(1.U)
      
      // Set up mask (all enabled)
      c.io.in.bits.mask.foreach(_.poke(true.B))

      // Set up operands
      // in1 (vector) = 0
      // in2 (scalar broadcast) = NaN
      // VFDIV.VF: vd[i] = vs2[i] / rs1
      // vs2 is in1. rs1 is in2.
      // So in1 = 0, in2 = NaN.
      
      for(i <- 0 until num_thread) {
        c.io.in.bits.in1(i).poke(0.U)
        c.io.in.bits.in2(i).poke("hfffffc0c".U)
        c.io.in.bits.in3(i).poke(0.U)
      }

      // Wait for acceptance
      while(!c.io.in.ready.peek().litToBoolean) {
        c.clock.step()
      }
      c.clock.step()
      c.io.in.valid.poke(false.B)

      // Wait for output
      var cycles = 0
      while(!c.io.out_v.valid.peek().litToBoolean && cycles < 200) {
        c.clock.step()
        cycles += 1
      }

      if (c.io.out_v.valid.peek().litToBoolean) {
        val result = c.io.out_v.bits.wb_wvd_rd(0).peek().litValue
        println(f"SFU Result(0): 0x$result%08x")
        assert(result == 0x7fc00000L, f"Expected 0x7fc00000, got 0x$result%08x")
      } else {
        fail("SFUexe timed out")
      }
    }
  }

  "SFUexe should return 0 if fp=false (IntDiv)" in {
    test(new SFUexe) { c =>
      c.io.in.valid.poke(false.B)
      c.io.out_v.ready.poke(true.B)
      c.io.out_x.ready.poke(true.B)
      c.io.rm.poke(0.U)
      c.clock.step()

      c.io.in.valid.poke(true.B)
      c.io.in.bits.ctrl.isvec.poke(true.B)
      c.io.in.bits.ctrl.fp.poke(false.B) // Force IntDiv
      c.io.in.bits.ctrl.wvd.poke(true.B)
      c.io.in.bits.ctrl.alu_fn.poke(0.U) // DIV
      c.io.in.bits.ctrl.wid.poke(1.U)
      c.io.in.bits.mask.foreach(_.poke(true.B))

      for(i <- 0 until num_thread) {
        c.io.in.bits.in1(i).poke(0.U)
        c.io.in.bits.in2(i).poke("hfffffc0c".U)
        c.io.in.bits.in3(i).poke(0.U)
      }

      while(!c.io.in.ready.peek().litToBoolean) c.clock.step()
      c.clock.step()
      c.io.in.valid.poke(false.B)

      var cycles = 0
      while(!c.io.out_v.valid.peek().litToBoolean && cycles < 200) {
        c.clock.step()
        cycles += 1
      }

      if (c.io.out_v.valid.peek().litToBoolean) {
        val result = c.io.out_v.bits.wb_wvd_rd(0).peek().litValue
        println(f"SFU Result(0) [IntDiv]: 0x$result%08x")
        assert(result == 0L, f"Expected 0, got 0x$result%08x")
      } else {
        fail("SFUexe timed out")
      }
    }
  }
}
