import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.util.task.ConsoleTaskMonitor;

import java.io.FileWriter;
import java.io.PrintWriter;

// Round 25: found the literal strings "!BackupServer" (0x80780870) and "baksrvs"
// (0x80780890) hardcoded in bafl.dll. Find every code reference TO this data (not
// just to the exact string start - descriptor headers a few bytes before the text
// itself are the more likely reference target) to identify which bafl.dll function
// actually uses it to connect to the backup server.
public class FindBaflStringRefs extends GhidraScript {
    private static final long[] TARGETS = {
        0x80780860L, 0x80780864L, 0x80780868L, 0x8078086cL, 0x80780870L,
        0x80780888L, 0x8078088cL, 0x80780890L
    };

    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/bafl_string_refs.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        Listing listing = currentProgram.getListing();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        ConsoleTaskMonitor monitor2 = new ConsoleTaskMonitor();

        for (long t : TARGETS) {
            Address addr = toAddr(t);
            ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(addr);
            out.printf("=== refs to 0x%x ===%n", t);
            boolean any = false;
            while (refs.hasNext()) {
                any = true;
                Reference r = refs.next();
                Address from = r.getFromAddress();
                Function containing = currentProgram.getFunctionManager().getFunctionContaining(from);
                out.printf("  from %s (in function %s)%n", from,
                    containing == null ? "?" : (containing.getName() + " @ " + containing.getEntryPoint()));

                if (containing != null) {
                    out.println("  --- disassembly ---");
                    InstructionIterator ins = listing.getInstructions(containing.getBody(), true);
                    while (ins.hasNext()) {
                        Instruction i = ins.next();
                        out.printf("  %s: %s%n", i.getAddress(), i.toString());
                    }
                    out.println("  --- decompiled ---");
                    DecompileResults res = decomp.decompileFunction(containing, 30, monitor2);
                    if (res.decompileCompleted()) {
                        out.println(res.getDecompiledFunction().getC());
                    } else {
                        out.println("  decompile failed: " + res.getErrorMessage());
                    }
                }
            }
            if (!any) {
                out.println("  (no refs)");
            }
            out.println();
        }

        out.close();
        println("WROTE " + outPath);
    }
}
