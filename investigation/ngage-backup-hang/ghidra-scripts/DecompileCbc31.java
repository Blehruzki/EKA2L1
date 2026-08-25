import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DecompileCbc31 extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/decompile_cbc31.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));
        DecompInterface decomp = new DecompInterface();
        decomp.setOptions(new DecompileOptions());
        decomp.openProgram(currentProgram);

        Address a = toAddr(0x804cbc31L & ~1L);
        Function f = getFunctionContaining(a);
        out.println("0x804cbc31 -> " + (f != null ? f.getName() + " @ " + f.getEntryPoint() : "(none)"));
        if (f != null) {
            DecompileResults res = decomp.decompileFunction(f, 30, monitor);
            if (res != null && res.decompileCompleted()) {
                out.println(res.getDecompiledFunction().getC());
            }
        }
        decomp.dispose();
        out.close();
        println("WROTE " + outPath);
    }
}
