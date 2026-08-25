import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.listing.Function;

import java.io.FileWriter;
import java.io.PrintWriter;

public class FindSplashThreadRefs extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/splash_thread_refs.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        DataIterator it = currentProgram.getListing().getDefinedData(true);
        int found = 0;
        while (it.hasNext()) {
            Data d = it.next();
            String val = null;
            try {
                Object v = d.getValue();
                if (v != null) val = v.toString();
            } catch (Exception e) {}
            if (val != null && val.contains("PlayServer")) {
                out.printf("STRING at %s: %s%n", d.getAddress(), val);
                Reference[] refs = getReferencesTo(d.getAddress());
                for (Reference r : refs) {
                    Address from = r.getFromAddress();
                    Function f = getFunctionContaining(from);
                    out.printf("  ref from %s (func: %s)%n", from, f != null ? f.getName() + "@" + f.getEntryPoint() : "?");
                }
                found++;
            }
        }
        out.printf("Total PlayServer-related strings found: %d%n", found);
        out.close();
        println("WROTE " + outPath);
    }
}
