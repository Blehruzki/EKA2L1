import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.listing.ProgramContext;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.math.BigInteger;

// Round 25 follow-up: auto-analysis left 0x80780864/6c/70/8c undisassembled
// (no function, no instructions). Force clearListing + Thumb + disassemble
// directly at each, plus a window before/after, to see what's actually there.
public class DumpBaflForced extends GhidraScript {
    private static final long[] CANDIDATES = { 0x80780864L, 0x8078086cL, 0x80780870L, 0x8078088cL };

    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/bafl_forced.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        ProgramContext ctx = currentProgram.getProgramContext();
        Register tmodeReg = ctx.getRegister("TMode");
        Listing listing = currentProgram.getListing();

        Address winStart = toAddr(0x80780820L);
        Address winEnd = toAddr(0x807808b0L);
        try {
            clearListing(winStart, winEnd);
            if (tmodeReg != null) {
                ctx.setValue(tmodeReg, winStart, winEnd, BigInteger.ONE);
            }
            disassemble(winStart);
        } catch (Exception e) {
            out.println("disassemble failed: " + e);
        }

        out.println("=== forced Thumb disassembly 0x80780820 - 0x807808b0 ===");
        InstructionIterator ins = listing.getInstructions(winStart, true);
        int count = 0;
        while (ins.hasNext() && count < 80) {
            Instruction i = ins.next();
            if (i.getAddress().compareTo(winEnd) > 0) break;
            out.printf("%s: %s%n", i.getAddress(), i.toString());
            count++;
        }

        out.println();
        out.println("=== raw bytes, same window ===");
        byte[] buf = new byte[(int) (winEnd.subtract(winStart) + 0x10)];
        int n = currentProgram.getMemory().getBytes(winStart, buf);
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < n; i++) {
            if (i % 16 == 0) sb.append(String.format("%n%s: ", winStart.add(i)));
            sb.append(String.format("%02x ", buf[i] & 0xff));
        }
        out.println(sb.toString());

        out.close();
        println("WROTE " + outPath);
    }
}
