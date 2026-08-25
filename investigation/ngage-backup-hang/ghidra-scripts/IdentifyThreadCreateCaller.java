import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;

import java.io.FileWriter;
import java.io.PrintWriter;

public class IdentifyThreadCreateCaller extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/thread_create_caller_chain.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        Address a = toAddr(0x803AA7EDL);
        Function f = getFunctionContaining(a);
        out.printf("Address 0x803AA7ED is in function: %s%n", f != null ? (f.getName() + "@" + f.getEntryPoint()) : "UNKNOWN (not in any defined function)");

        if (f != null) {
            Address entry = f.getEntryPoint();
            Reference[] refs = getReferencesTo(entry);
            out.printf("Callers of %s (%d found):%n", f.getName(), refs.length);
            for (Reference r : refs) {
                Address from = r.getFromAddress();
                Function callerFunc = getFunctionContaining(from);
                out.printf("  called from 0x%s (in func: %s)%n", from, callerFunc != null ? (callerFunc.getName() + "@" + callerFunc.getEntryPoint()) : "?");
            }
        }
        out.close();
        println("WROTE " + outPath);
    }
}
