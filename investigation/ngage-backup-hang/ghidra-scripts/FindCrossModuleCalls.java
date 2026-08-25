import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.scalar.Scalar;
import ghidra.util.task.ConsoleTaskMonitor;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.LinkedHashMap;
import java.util.Map;

// Scans ONLY ecom.dll's and ecomserver.exe's own code blocks for any bl/blx
// instruction whose target lands inside euser.dll's address range - i.e. every
// place the application actually calls into the base library.
public class FindCrossModuleCalls extends GhidraScript {
    private static final long EUSER_LO = 0x8039CDB8L;
    private static final long EUSER_HI = 0x803CF328L;

    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/ghidra_crossmodule_result.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();

        for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
            String name = block.getName();
            if (!(name.equals("ecom.dll") || name.equals("ecomserver.exe"))) continue;

            out.println("### scanning block: " + name + " [" + block.getStart() + " - " + block.getEnd() + "]");

            Map<String, Function> callerFuncs = new LinkedHashMap<>();
            InstructionIterator ins = listing.getInstructions(block.getStart(), true);
            while (ins.hasNext()) {
                Instruction i = ins.next();
                if (i.getAddress().compareTo(block.getEnd()) > 0) break;
                String mnem = i.getMnemonicString();
                if (!mnem.toLowerCase().startsWith("bl")) continue;

                for (int k = 0; k < i.getNumOperands(); k++) {
                    for (Object o : i.getOpObjects(k)) {
                        Long target = null;
                        if (o instanceof Address) target = ((Address) o).getOffset();
                        else if (o instanceof Scalar) target = ((Scalar) o).getUnsignedValue();
                        if (target != null && target >= EUSER_LO && target <= EUSER_HI) {
                            out.printf("  CALL at %s -> 0x%x   (%s)%n", i.getAddress(), target, i.toString());
                            Function f = fm.getFunctionContaining(i.getAddress());
                            if (f != null) callerFuncs.putIfAbsent(f.getEntryPoint().toString(), f);
                        }
                    }
                }
            }
            out.printf("  %d unique caller functions in this block%n%n", callerFuncs.size());

            DecompInterface decomp = new DecompInterface();
            decomp.openProgram(currentProgram);
            ConsoleTaskMonitor monitor2 = new ConsoleTaskMonitor();

            for (Map.Entry<String, Function> e : callerFuncs.entrySet()) {
                Function func = e.getValue();
                out.println("  ---------------------------------------------------------");
                out.printf("  FUNCTION %s @ %s size=%d%n", func.getName(), func.getEntryPoint(), func.getBody().getNumAddresses());
                out.println("  ---------------------------------------------------------");

                InstructionIterator ins2 = listing.getInstructions(func.getBody(), true);
                while (ins2.hasNext()) {
                    Instruction i2 = ins2.next();
                    out.printf("  %s: %s%n", i2.getAddress(), i2.toString());
                }

                out.println("  --- decompiled ---");
                DecompileResults res = decomp.decompileFunction(func, 60, monitor2);
                if (res.decompileCompleted()) {
                    out.println(res.getDecompiledFunction().getC());
                } else {
                    out.println("  decompile failed: " + res.getErrorMessage());
                }
                out.println();
            }
        }

        out.close();
        println("WROTE RESULT TO " + outPath);
    }
}
