import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

import java.io.FileWriter;
import java.io.PrintWriter;

public class CheckMsgCompleteAddrs extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/msg_complete_addrs.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));
        DecompInterface decomp = new DecompInterface();
        decomp.setOptions(new DecompileOptions());
        decomp.openProgram(currentProgram);
        long[] targets = { 0x8039fd70L, 0x8039fd78L };
        for (long t : targets) {
            Address a = toAddr(t);
            Function f = getFunctionContaining(a);
            out.printf("0x%x -> %s%n", t, f != null ? f.getName() + " @ " + f.getEntryPoint() : "(none)");
            if (f != null) {
                DecompileResults res = decomp.decompileFunction(f, 20, monitor);
                if (res != null && res.decompileCompleted()) {
                    out.println(res.getDecompiledFunction().getC());
                }
            }
        }
        decomp.dispose();
        out.close();
        println("WROTE " + outPath);
    }
}
