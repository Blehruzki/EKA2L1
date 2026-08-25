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

public class FindCallersOfInit extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/callers_of_init_r63.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager rm = currentProgram.getReferenceManager();
        Listing listing = currentProgram.getListing();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);

        Address target = toAddr(0x700002caL);
        out.printf("=== callers of FUN_700002ca ===%n");
        ReferenceIterator refs = rm.getReferencesTo(target);
        int count = 0;
        while (refs.hasNext()) {
            Reference r = refs.next();
            Address from = r.getFromAddress();
            Function f = fm.getFunctionContaining(from);
            out.printf("  from %s (in %s) type=%s%n", from, f != null ? f.getName() + "@" + f.getEntryPoint() : "???", r.getReferenceType());
            count++;
        }
        out.printf("total: %d%n%n", count);

        // Also decompile each caller function fully
        refs = rm.getReferencesTo(target);
        java.util.Set<Address> seen = new java.util.HashSet<>();
        while (refs.hasNext()) {
            Reference r = refs.next();
            Address from = r.getFromAddress();
            Function f = fm.getFunctionContaining(from);
            if (f != null && !seen.contains(f.getEntryPoint())) {
                seen.add(f.getEntryPoint());
                out.printf("=== caller function %s @ %s size=%d ===%n", f.getName(), f.getEntryPoint(), f.getBody().getNumAddresses());
                InstructionIterator instrs = listing.getInstructions(f.getBody(), true);
                int icount = 0;
                while (instrs.hasNext()) {
                    Instruction i = instrs.next();
                    out.printf("  %s: %s%n", i.getAddress(), i);
                    icount++;
                    if (icount > 100) {
                        out.printf("  ... truncated%n");
                        break;
                    }
                }
                DecompileResults res = decomp.decompileFunction(f, 30, new ConsoleTaskMonitor());
                if (res.decompileCompleted()) {
                    out.printf("--- decompiled ---%n%s%n", res.getDecompiledFunction().getC());
                }
                out.println();
            }
        }

        out.close();
        println("WROTE " + outPath);
    }
}
