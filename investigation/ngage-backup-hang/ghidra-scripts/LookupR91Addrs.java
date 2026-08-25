import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

import java.io.FileWriter;
import java.io.PrintWriter;

public class LookupR91Addrs extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/lookup_r91_result.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        long[] targets = { 0x8039fa9cL, 0x803a02dcL, 0x803a02c8L };
        for (long target : targets) {
            Address a = toAddr(target);
            Function f = getFunctionContaining(a);
            out.printf("0x%x -> %s%n", target, (f != null ? f.getName() + " @ " + f.getEntryPoint() : "(no function)"));
        }
        out.close();
        println("WROTE " + outPath);
    }
}
