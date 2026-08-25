import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DecompileStubCluster extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/stub_cluster_decompile.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        DecompInterface decomp = new DecompInterface();
        decomp.setOptions(new DecompileOptions());
        decomp.openProgram(currentProgram);

        Address start = toAddr(0x803a0000L);
        Address end = toAddr(0x803a0500L);
        FunctionIterator fiter = currentProgram.getListing().getFunctions(start, true);
        while (fiter.hasNext()) {
            Function f = fiter.next();
            if (f.getEntryPoint().compareTo(end) > 0) break;
            out.println("==== " + f.getName() + " @ " + f.getEntryPoint() + " ====");
            try {
                DecompileResults res = decomp.decompileFunction(f, 20, monitor);
                if (res != null && res.decompileCompleted()) {
                    out.println(res.getDecompiledFunction().getC());
                }
            } catch (Exception e) {}
        }
        decomp.dispose();
        out.close();
        println("WROTE " + outPath);
    }
}
