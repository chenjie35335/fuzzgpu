// ./mill "ventus[6.4.0].tests.testOnly" play.FloatDivTest
package play

import chisel3._
import chiseltest._
import org.scalatest.freespec.AnyFreeSpec
import pipeline.FloatDivSqrt
import pipeline.FloatPoint

class FloatDivTest extends AnyFreeSpec with ChiselScalatestTester {
  "FloatDivSqrt should handle 0 / NaN correctly" in {
    test(new FloatDivSqrt) { c =>
      // 0 / 0xfffffc0c
      // a = 0
      // b = 0xfffffc0c (NaN)
      // op = 0 (Div)
      // rm = 0 (RNE)

      c.io.in.bits.a.poke(0.U)
      c.io.in.bits.b.poke("hfffffc0c".U)
      c.io.in.bits.op.poke(0.U) // Div
      c.io.in.bits.rm.poke(0.U)
      c.io.in.valid.poke(true.B)
      c.clock.step()
      c.io.in.valid.poke(false.B)

      // Wait for output
      var cycles = 0
      while(!c.io.out.valid.peek().litToBoolean && cycles < 100) {
        c.clock.step()
        cycles += 1
      }

      val result = c.io.out.bits.result.peek().litValue
      val flags = c.io.out.bits.flags.invalid.peek().litToBoolean
      
      println(f"Result: 0x$result%08x")
      println(f"Invalid Flag: $flags")

      // Expected: NaN (0x7fc00000)
      assert(result == 0x7fc00000L, f"Expected 0x7fc00000, got 0x$result%08x")
    }
  }

  "FloatPoint layout check" in {
    test(new Module {
      val io = IO(new Bundle {
        val in = Input(UInt(32.W))
        val sign = Output(Bool())
        val exp = Output(UInt(8.W))
        val frac = Output(UInt(23.W))
      })
      val fp = io.in.asTypeOf(new FloatPoint(8, 23))
      io.sign := fp.sign
      io.exp := fp.exp
      io.frac := fp.frac
    }) { c =>
      // 0xfffffc0c
      // Sign: 1
      // Exp: 0xff
      // Frac: 0x7ffc0c
      c.io.in.poke("hfffffc0c".U)
      val sign = c.io.sign.peek().litToBoolean
      val exp = c.io.exp.peek().litValue
      val frac = c.io.frac.peek().litValue
      
      println(f"Input: 0xfffffc0c")
      println(f"Sign: $sign")
      println(f"Exp: 0x$exp%x")
      println(f"Frac: 0x$frac%x")

      assert(sign == true)
      assert(exp == 0xff)
      assert(frac == 0x7ffc0c)
    }
  }
}
