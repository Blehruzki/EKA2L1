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

// rm-409's baksrv.dll (already imported, analyzed) has the same drtrvct2
// switch-trampoline byte pattern round 19 found in rm-320's, confirmed at
// 0x80196fe8, reached via thunk_EXT_FUN_80196fe8 @ 0x80b85cc8. Find every caller of
// that thunk (baksrv.dll's own switch-dispatch site(s), analogous to rm-320's
// 0x821b6190-area call), then dump each caller function's disassembly so the
// switch table and case count can be read the same way round 19 did for rm-320.
public class FindRm409SwitchCallers extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/rm409_switch_callers.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        Address thunkAddr = toAddr(0x80b85cc8L);
        Listing listing = currentProgram.getListing();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        ConsoleTaskMonitor monitor2 = new ConsoleTaskMonitor();

        ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(thunkAddr);
        int count = 0;
        while (refs.hasNext()) {
            Reference r = refs.next();
            Address from = r.getFromAddress();
            Function containing = currentProgram.getFunctionManager().getFunctionContaining(from);
            out.printf("=== ref from %s (in function %s) ===%n", from,
                containing == null ? "?" : (containing.getName() + " @ " + containing.getEntryPoint()));
            count++;

            if (containing != null) {
                out.println("--- disassembly ---");
                InstructionIterator ins = listing.getInstructions(containing.getBody(), true);
                while (ins.hasNext()) {
                    Instruction i = ins.next();
                    out.printf("%s: %s%n", i.getAddress(), i.toString());
                }
                out.println("--- decompiled ---");
                DecompileResults res = decomp.decompileFunction(containing, 30, monitor2);
                if (res.decompileCompleted()) {
                    out.println(res.getDecompiledFunction().getC());
                } else {
                    out.println("decompile failed: " + res.getErrorMessage());
                }
            }
            out.println();
        }
        out.println("TOTAL REFS: " + count);

        out.close();
        println("WROTE " + outPath + " refs=" + count);
    }
}
