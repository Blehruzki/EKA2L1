import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

import java.io.FileWriter;
import java.io.PrintWriter;

public class TraceMutexWaitCallers extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/trace_mutex_wait_r60.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager rm = currentProgram.getReferenceManager();

        long[] targets = { 0x8039fa9cL, 0x803a82e4L };
        for (long t : targets) {
            Address a = toAddr(t);
            Function f = fm.getFunctionContaining(a);
            out.printf("=== address 0x%x ===%n", t);
            if (f == null) {
                out.printf("  no function contains this address%n%n");
                continue;
            }
            out.printf("  in function: %s @ %s (size=%d)%n", f.getName(), f.getEntryPoint(), f.getBody().getNumAddresses());

            // find callers of this function
            Address entry = f.getEntryPoint();
            ReferenceIterator refs = rm.getReferencesTo(entry);
            int count = 0;
            out.printf("  callers of %s:%n", f.getName());
            while (refs.hasNext()) {
                Reference r = refs.next();
                Address from = r.getFromAddress();
                Function callerFn = fm.getFunctionContaining(from);
                out.printf("    from %s (in %s) type=%s%n", from,
                    callerFn != null ? callerFn.getName() + "@" + callerFn.getEntryPoint() : "???", r.getReferenceType());
                count++;
                if (count > 50) {
                    out.printf("    ... (truncated)%n");
                    break;
                }
            }
            out.printf("  total callers: %d%n%n", count);
        }

        out.close();
        println("WROTE " + outPath);
    }
}
