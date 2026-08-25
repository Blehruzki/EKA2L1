import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

import java.io.FileWriter;
import java.io.PrintWriter;

public class FindContainingFunc extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/containing_func_result.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        long[] targets = { 0x803a02ccL, 0x8039fa98L };
        for (long target : targets) {
            Address a = toAddr(target);
            Function f = getFunctionContaining(a);
            out.printf("0x%x contained in function starting 0x%s (name %s)%n", target,
                f != null ? f.getEntryPoint() : "none", f != null ? f.getName() : "none");
            // Also list who calls the CONTAINING function itself (its own entry)
            if (f != null) {
                ghidra.program.model.symbol.ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(f.getEntryPoint());
                int count = 0;
                while (refs.hasNext()) {
                    ghidra.program.model.symbol.Reference r = refs.next();
                    count++;
                }
                out.printf("  callers of entry 0x%s: %d%n", f.getEntryPoint(), count);
            }
        }
        out.close();
        println("WROTE " + outPath);
    }
}
