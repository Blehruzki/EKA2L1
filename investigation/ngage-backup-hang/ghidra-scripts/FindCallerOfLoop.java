import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.util.task.ConsoleTaskMonitor;

import java.io.FileWriter;
import java.io.PrintWriter;

public class FindCallerOfLoop extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/caller_of_loop_r64.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager rm = currentProgram.getReferenceManager();
        Listing listing = currentProgram.getListing();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);

        // callers of FUN_70800206
        Address target = toAddr(0x70800206L);
        out.printf("=== callers of FUN_70800206 ===%n");
        ReferenceIterator refs = rm.getReferencesTo(target);
        java.util.Set<Address> seen = new java.util.HashSet<>();
        while (refs.hasNext()) {
            Reference r = refs.next();
            Address from = r.getFromAddress();
            Function f = fm.getFunctionContaining(from);
            out.printf("  from %s (in %s) type=%s%n", from, f != null ? f.getName() + "@" + f.getEntryPoint() : "???", r.getReferenceType());
            if (f != null) seen.add(f.getEntryPoint());
        }
        out.println();

        for (Address entry : seen) {
            Function f = fm.getFunctionAt(entry);
            out.printf("=== caller function %s @ %s size=%d ===%n", f.getName(), f.getEntryPoint(), f.getBody().getNumAddresses());
            InstructionIterator instrs = listing.getInstructions(f.getBody(), true);
            int icount = 0;
            while (instrs.hasNext()) {
                Instruction i = instrs.next();
                out.printf("  %s: %s%n", i.getAddress(), i);
                icount++;
                if (icount > 150) { out.printf("  ... truncated%n"); break; }
            }
            DecompileResults res = decomp.decompileFunction(f, 30, new ConsoleTaskMonitor());
            if (res.decompileCompleted()) {
                out.printf("--- decompiled ---%n%s%n", res.getDecompiledFunction().getC());
            }
            out.println();
        }

        // also decompile FUN_70801b7e
        Address t2 = toAddr(0x70801b7eL);
        Function f2 = fm.getFunctionContaining(t2);
        out.printf("=== FUN_70801b7e ===%n");
        if (f2 != null) {
            out.printf("function %s @ %s size=%d%n", f2.getName(), f2.getEntryPoint(), f2.getBody().getNumAddresses());
            InstructionIterator instrs = listing.getInstructions(f2.getBody(), true);
            int icount = 0;
            while (instrs.hasNext()) {
                Instruction i = instrs.next();
                out.printf("  %s: %s%n", i.getAddress(), i);
                icount++;
                if (icount > 80) { out.printf("  ... truncated%n"); break; }
            }
            DecompileResults res = decomp.decompileFunction(f2, 30, new ConsoleTaskMonitor());
            if (res.decompileCompleted()) {
                out.printf("--- decompiled ---%n%s%n", res.getDecompiledFunction().getC());
            }
        } else {
            out.printf("no function%n");
        }

        out.close();
        println("WROTE " + outPath);
    }
}
