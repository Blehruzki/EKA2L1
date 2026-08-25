import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.util.task.ConsoleTaskMonitor;

import java.io.FileWriter;
import java.io.PrintWriter;

// Same as DumpBaksrvs but leaves the default ARM (TMode=0) mode alone instead
// of forcing Thumb - raw bytes near the end of the file look like classic ARM
// LDR-PC-relative import veneers (04 F0 1F E5 = LDR PC,[PC,#-4]), suggesting
// this tiny stub binary is (at least partly) ARM mode, unlike the big DLLs
// from earlier rounds which were confirmed all-Thumb.
public class DumpBaksrvsArm extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/baksrvs_dump_arm.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        Listing listing = currentProgram.getListing();

        for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
            Address start = block.getStart();
            disassemble(start);
            createFunction(start, "entry_" + start);
        }
        analyzeAll(currentProgram);

        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        ConsoleTaskMonitor monitor2 = new ConsoleTaskMonitor();

        FunctionIterator fit = listing.getFunctions(true);
        int count = 0;
        while (fit.hasNext()) {
            Function func = fit.next();
            count++;
            out.printf("=== FUNCTION %s @ %s size=%d ===%n", func.getName(), func.getEntryPoint(), func.getBody().getNumAddresses());
            out.println("--- disassembly ---");
            InstructionIterator ins = listing.getInstructions(func.getBody(), true);
            while (ins.hasNext()) {
                Instruction i = ins.next();
                out.printf("%s: %s%n", i.getAddress(), i.toString());
            }
            out.println("--- decompiled ---");
            DecompileResults res = decomp.decompileFunction(func, 60, monitor2);
            if (res.decompileCompleted()) {
                out.println(res.getDecompiledFunction().getC());
            } else {
                out.println("decompile failed: " + res.getErrorMessage());
            }
            out.println();
        }
        out.println("TOTAL FUNCTIONS: " + count);
        out.close();
        println("WROTE " + outPath + " funcs=" + count);
    }
}
