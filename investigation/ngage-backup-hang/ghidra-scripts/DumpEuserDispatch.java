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

// Round 16 found that the live trace steps through 0x80399f48-0x80399f5c in
// 4-byte strides (pc=80399f48,4c,50,54,58,5c) - that's ARM mode (fixed 4-byte
// instructions), not Thumb like the rest of this investigation's code has been.
// This is almost certainly the point where a per-opcode handler pointer gets
// looked up and jumped to (func=31 and func=32 land on different addresses,
// 0x821b6226 vs 0x821b622e, from the same call site with identical lr). Disassemble
// it in ARM mode (TMode=0, explicit) to find the actual dispatch/lookup logic.
public class DumpEuserDispatch extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/euser_dispatch.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        ProgramContext ctx = currentProgram.getProgramContext();
        Register tmodeReg = ctx.getRegister("TMode");
        Listing listing = currentProgram.getListing();

        Address start = toAddr(0x80399f48L);
        Address end = start.add(0x100);
        clearListing(start, end);
        if (tmodeReg != null) {
            // Explicit ARM mode (0), not Thumb - the live trace's 4-byte-per-step
            // stride through this exact region proves it's ARM, unlike the rest of
            // this codebase which has been confirmed Thumb everywhere else.
            ctx.setValue(tmodeReg, start, end, BigInteger.ZERO);
        }
        disassemble(start);

        out.println("=== ARM-mode disassembly from 0x80399f48 ===");
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
