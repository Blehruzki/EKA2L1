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

// Follow-up to DumpFunc31vs32Handlers: that script showed 0x821b6226 is NOT a
// separate jump-table case - it's a linear instruction sequence
// (ldr r1,[r1,#0]; movs r0,r4; bl 0x821b5c60; str r6,[r5,#0]; pop {...}) and
// 0x821b622e is just the address right after the "bl 0x821b5c60" call, inside the
// same sequence. So func=31's live trace runs the whole thing (calls 0x821b5c60),
// while func=32's trace starts stepping at 0x821b622e directly - meaning something
// BEFORE 0x821b6226 branches around the call entirely for func=32. Disassemble a
// wider window before/after to find that branch and what it's testing, and also
// dump the target of the skipped call (0x821b5c60) since that's plausibly the real
// per-message handler that would reach message_complete.
public class DumpFunc32SkipBranch extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/func32_skip_branch.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        ProgramContext ctx = currentProgram.getProgramContext();
        Register tmodeReg = ctx.getRegister("TMode");
        Listing listing = currentProgram.getListing();

        // Window 1: well before the split, through the split, to well after -
        // wide enough to catch the branch instruction that decides which path.
        Address w1start = toAddr(0x821b6180L);
        Address w1end = toAddr(0x821b6240L);
        clearListing(w1start, w1end);
        if (tmodeReg != null) {
            ctx.setValue(tmodeReg, w1start, w1end, BigInteger.ONE);
        }
        disassemble(w1start);

        out.println("=== window 0x821b6180 - 0x821b6240 (branch search) ===");
        InstructionIterator ins = listing.getInstructions(w1start, true);
        while (ins.hasNext()) {
            Instruction i = ins.next();
            if (i.getAddress().compareTo(w1end) > 0) break;
            out.printf("%s: %s%n", i.getAddress(), i.toString());
        }
        out.println();

        // Window 2: the skipped call target, 0x821b5c60.
        Address w2start = toAddr(0x821b5c60L);
        Address w2end = w2start.add(0x80);
        clearListing(w2start, w2end);
        if (tmodeReg != null) {
            ctx.setValue(tmodeReg, w2start, w2end, BigInteger.ONE);
        }
        disassemble(w2start);

        out.println("=== window 0x821b5c60 (the call func=32 skips) ===");
        InstructionIterator ins2 = listing.getInstructions(w2start, true);
        int count = 0;
        while (ins2.hasNext() && count < 60) {
            Instruction i = ins2.next();
            out.printf("%s: %s%n", i.getAddress(), i.toString());
            count++;
        }

        out.close();
        println("WROTE " + outPath);
    }
}
