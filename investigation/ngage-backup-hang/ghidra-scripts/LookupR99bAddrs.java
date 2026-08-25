import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

import java.io.FileWriter;
import java.io.PrintWriter;

public class LookupR99bAddrs extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/lookup_r99b_addrs.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));
        long[] targets = { 0x803ac8b3L, 0x803ac913L, 0x803ac9b5L, 0x803f688bL };
        for (long t : targets) {
            Address a = toAddr(t);
            Function f = getFunctionContaining(a);
            out.printf("0x%x -> %s (in range: %b)%n", t, f != null ? f.getName() + " @ " + f.getEntryPoint() : "(none)",
                currentProgram.getMemory().contains(a));
        }
        out.close();
        println("WROTE " + outPath);
    }
}
