import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Instruction;

import java.io.FileWriter;
import java.io.PrintWriter;

// Round 52 follow-up: the LR values captured live (0x83485ffb, 0x8348600d) are ODD
// (Thumb-mode return marker), so the real return address is LR-1. The call
// instruction itself should occupy the 4 bytes immediately before that (a 32-bit
// Thumb-2 BL/BLX). Round 52's first pass showed a flow-based disassembly window
// that didn't line up (an unconditional "b" appeared to skip right over this
// region) - a classic Thumb/data alignment trap this file has hit before (round 38).
// Force-disassemble exactly at the real return address and the 4 bytes before it,
// in Thumb mode, to get the real call instruction rather than trust flow-following.
public class DumpCdlserverCallInstr extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/cdlserver_call_instr.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        long[] rawLrs = { 0x83485ffbL, 0x8348600dL };
        Register tmodeReg = currentProgram.getLanguage().getRegister("TMode");

        for (long rawLr : rawLrs) {
            long realReturn = rawLr - 1;
            out.printf("=== raw lr=%s -> real return=%s ===%n", Long.toHexString(rawLr), Long.toHexString(realReturn));

            // Dump raw bytes for a window straddling [return-16, return+4)
            Address winStart = toAddr(realReturn - 16);
            byte[] buf = new byte[20];
            try {
                currentProgram.getMemory().getBytes(winStart, buf);
                StringBuilder hex = new StringBuilder();
                for (byte b : buf) hex.append(String.format("%02x ", b));
                out.printf("raw bytes [%s..]: %s%n", winStart, hex.toString());
            } catch (Exception e) {
                out.println("byte read failed: " + e.getMessage());
            }

            // Force Thumb-mode disassembly starting at each candidate alignment,
            // reading the result immediately before trying the next (each attempt
            // clears/overwrites the same overlapping bytes, so read right away).
            out.println("--- disassembly attempts from various back-offsets (each independent) ---");
            for (long backOff : new long[] { 16, 12, 8, 6, 4, 2 }) {
                Address a = toAddr(realReturn - backOff);
                try {
                    if (tmodeReg != null) {
                        currentProgram.getProgramContext().setValue(tmodeReg, a, a, java.math.BigInteger.ONE);
                    }
                    clearListing(a, toAddr(realReturn + 2));
                    disassemble(a);
                } catch (Exception e) {
                    out.printf("[back=%d] disassemble failed: %s%n", backOff, e.getMessage());
                    continue;
                }

                Instruction i = getInstructionAt(a);
                StringBuilder sb = new StringBuilder();
                Address cur = a;
                for (int k = 0; k < 4 && cur.compareTo(toAddr(realReturn + 2)) < 0; k++) {
                    Instruction ii = getInstructionAt(cur);
                    if (ii == null) break;
                    sb.append(String.format("%s: %s | ", cur, ii.toString()));
                    cur = cur.add(ii.getLength());
                }
                out.printf("[back=%d] start=%s -> %s%n", backOff, a, i != null ? sb.toString() : "(no instruction / mid-instruction)");
            }
            out.println();
        }

        out.close();
        println("WROTE " + outPath);
    }
}
