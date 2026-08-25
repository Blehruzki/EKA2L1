import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.io.FileWriter;
import java.io.PrintWriter;

public class CheckPluginRefs extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/plugin_refs_result.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath, true));

        out.println("==== PROGRAM " + currentProgram.getName() + " ====");
        long[] targets = { 0x803aa998L, 0x803a02ccL, 0x8039fa98L };
        String[] labels = { "SendReceive(FUN_803aa998)", "raw SVC 0x4c stub", "raw SVC 0x800000 (generic wait)" };
        for (int i = 0; i < targets.length; i++) {
            Address a = toAddr(targets[i]);
            ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(a);
            int count = 0;
            while (refs.hasNext()) {
                Reference r = refs.next();
                Address from = r.getFromAddress();
                Function f = getFunctionContaining(from);
                out.printf("  [%s] ref from 0x%s in %s%n", labels[i], from, f != null ? f.getName() + " @ " + f.getEntryPoint() : "(none)");
                count++;
            }
            out.printf("  [%s] total: %d%n", labels[i], count);
        }
        out.println();
        out.close();
        println("WROTE " + outPath);
    }
}
