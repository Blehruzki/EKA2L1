import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.util.task.ConsoleTaskMonitor;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DecompileGtmdserverExe extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/gtmdserver_exe_all_funcs_r147.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);

        FunctionIterator fit = currentProgram.getFunctionManager().getFunctions(true);
        int n = 0;
        while (fit.hasNext()) {
            Function f = fit.next();
            if (f.isThunk() || f.isExternal()) continue;
            n++;
            out.printf("=== %s @ %s size=%d ===%n", f.getName(), f.getEntryPoint(), f.getBody().getNumAddresses());
            DecompileResults res = decomp.decompileFunction(f, 30, new ConsoleTaskMonitor());
            if (res.decompileCompleted()) {
                out.printf("%s%n", res.getDecompiledFunction().getC());
            } else {
                out.printf("decompile failed: %s%n", res.getErrorMessage());
            }
        }
        out.printf("%ntotal functions: %d%n", n);

        out.close();
        println("WROTE " + outPath);
    }
}
