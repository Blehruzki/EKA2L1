import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.listing.ProgramContext;
import ghidra.program.model.mem.MemoryBlock;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.math.BigInteger;

// Round 18 found via live register tracing that r3 differs by exactly 4 between
// function=31 and function=32 (0x49 vs 0x4d) right as both traces land on their
// respective per-opcode handler (0x821b6226 / 0x821b622e), landing in a SINGLE
// instruction step from inside drtrvct2 (0x80399f48-0x80399f5c) - meaning the actual
// table-indexed jump happens inside drtrvct2's own code, using r3 as the table
// offset. Round 17 found this address isn't in any previously-extracted project;
// this script imports drtrvct2_1.dll fresh (code_address=0x80399088, code_size=
// 0x1c04 per parse_rom.py) and disassembles in ARM mode (justified by the live
// trace's 4-byte-per-step stride through 0x80399f48-0x80399f5c) to find the actual
// table-read/branch instruction and the table itself.
public class DumpDrtrvct2Dispatch extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/drtrvct2_dispatch.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        ProgramContext ctx = currentProgram.getProgramContext();
        Register tmodeReg = ctx.getRegister("TMode");

        out.println("=== memory blocks ===");
        for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
            out.printf("  %s: %s - %s executable=%s%n", b.getName(), b.getStart(), b.getEnd(), b.isExecute());
        }
        out.println();

        Address start = toAddr(0x80399f48L);
        Address end = start.add(0x200);

        clearListing(start, end);
        if (tmodeReg != null) {
            // ARM mode (0), justified by the live trace's 4-byte-per-step stride
            // through this exact region (0x80399f48, 4c, 50, 54, 58, 5c).
            ctx.setValue(tmodeReg, start, end, BigInteger.ZERO);
        }
        disassemble(start);

        out.println("=== ARM-mode disassembly from 0x80399f48 (freshly imported drtrvct2) ===");
        Listing listing = currentProgram.getListing();
        InstructionIterator ins = listing.getInstructions(start, true);
        int count = 0;
        while (ins.hasNext() && count < 100) {
            Instruction i = ins.next();
            if (i.getAddress().compareTo(end) > 0) break;
            out.printf("%s: %s%n", i.getAddress(), i.toString());
            count++;
        }

        out.println();
        out.println("=== raw bytes 0x80399f30 - 0x80399fc0 (context, in case disasm misaligns) ===");
        Address ctxStart = toAddr(0x80399f30L);
        byte[] buf = new byte[0x90];
        try {
            int n = currentProgram.getMemory().getBytes(ctxStart, buf);
            StringBuilder sb = new StringBuilder();
            for (int i = 0; i < n; i++) {
                if (i % 16 == 0) sb.append(String.format("%n%s: ", ctxStart.add(i)));
                sb.append(String.format("%02x ", buf[i] & 0xff));
            }
            out.println(sb.toString());
        } catch (Exception e) {
            out.println("getBytes failed: " + e);
        }

        out.close();
        println("WROTE " + outPath);
    }
}
