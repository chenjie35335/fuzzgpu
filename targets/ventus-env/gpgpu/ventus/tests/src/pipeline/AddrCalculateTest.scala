package pipeline

import chisel3._
import chisel3.util._
import chiseltest._
import org.scalatest.freespec.AnyFreeSpec
import top.parameters._

class AddrCalculateTest extends AnyFreeSpec with ChiselScalatestTester {
  "AddrCalculate should calculate correct address for vlb.v with negative immediate" in {
    test(new AddrCalculate) { c =>
      // Initialize inputs
      c.io.sm_id.poke(0.U)
      c.io.csr_pds.poke("h78000000".U)
      c.io.csr_numw.poke(2.U)
      c.io.csr_numt.poke(16.U)
      c.io.csr_tid.poke(16.U)
      
      c.io.from_fifo.valid.poke(true.B)
      c.io.flush_dcache.valid.poke(false.B)
      
      // Set up vExeData
      // in1 is v9 (index), set to 0 for thread 0
      // in2 is immediate -791 = 0xFFFFFCE9
      val imm = BigInt("FFFFFCE9", 16)
      
      for (i <- 0 until num_thread) {
        c.io.from_fifo.bits.in1(i).poke(0.U)
        c.io.from_fifo.bits.in2(i).poke(imm.U)
        c.io.from_fifo.bits.mask(i).poke(true.B)
      }
      
      // Control signals for vlb.v
      c.io.from_fifo.bits.ctrl.isvec.poke(true.B)
      c.io.from_fifo.bits.ctrl.disable_mask.poke(true.B)
      c.io.from_fifo.bits.ctrl.is_vls12.poke(false.B)
      c.io.from_fifo.bits.ctrl.mem_cmd.poke(2.U) // Read
      c.io.from_fifo.bits.ctrl.wid.poke(0.U)
      
      // Step to latch data into reg_save (state s_idle -> s_save)
      c.clock.step(1)
      
      // Check address calculation
      if (c.io.to_dcache.bits.spike_info.isDefined) {
        val vaddr = c.io.to_dcache.bits.spike_info.get.vaddr.peek().litValue
        println(f"Calculated vaddr: 0x$vaddr%x")
        
        // Expected correct value: 0x77ff9d41
        assert(vaddr == BigInt("77ff9d41", 16), f"Expected correct address 0x77ff9d41, got 0x$vaddr%x")
      } else {
        println("SPIKE_OUTPUT is not enabled, cannot check vaddr directly.")
      }
    }
  }
}
