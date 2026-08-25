import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.io.FileWriter;
import java.io.PrintWriter;

public class FindSendReceiveCaller extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/sendreceive_caller_result.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        Address a = toAddr(0x803a02ccL);
        ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(a);
        while (refs.hasNext()) {
            Reference r = refs.next();
            Address from = r.getFromAddress();
            Function f = getFunctionContaining(from);
            out.printf("ref from 0x%s in function %s%n", from, f != null ? f.getName() + " @ " + f.getEntryPoint() : "(none)");
            if (f != null) {
                // Now find callers of THIS function (the real SendReceive wrapper)
                ReferenceIterator refs2 = currentProgram.getReferenceManager().getReferencesTo(f.getEntryPoint());
                int count = 0;
                while (refs2.hasNext()) {
                    refs2.next();
                    count++;
                }
                out.printf("  this function (0x%s) has %d callers total in euser.dll itself%n", f.getEntryPoint(), count);
            }
        }
        out.close();
        println("WROTE " + outPath);
    }
}
