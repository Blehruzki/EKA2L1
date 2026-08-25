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

// Prior disassembly attempts starting from a guessed entry point (0x821b6180)
// produced instruction boundaries that don't line up with the live single-step
// trace's own return address (lr=0x821b6195, which fell mid-instruction in that
// disassembly) - the same Thumb-alignment trap every earlier round hit. Disassemble
// starting EXACTLY at the live, confirmed-correct address (0x821b6195, the actual
// return site after the call both func=31 and func=32 make) instead of guessing an
// entry point, to get correctly-aligned instructions through the real branch point.
public class DumpFromLiveLr extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/from_live_lr.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        ProgramContext ctx = currentProgram.getProgramContext();
        Register tmodeReg = ctx.getRegister("TMode");
        Listing listing = currentProgram.getListing();

        Address start = toAddr(0x821b6195L);
        Address end = start.add(0x100);
        clearListing(start, end);
        if (tmodeReg != null) {
            ctx.setValue(tmodeReg, start, end, BigInteger.ONE);
        }
        disassemble(start);

        out.println("=== disassembly from live lr=0x821b6195 ===");
        InstructionIterator ins = listing.getInstructions(start, true);
        int count = 0;
        while (ins.hasNext() && count < 60) {
            Instruction i = ins.next();
            out.printf("%s: %s%n", i.getAddress(), i.toString());
            count++;
        }

        out.close();
        println("WROTE " + outPath);
    }
}
