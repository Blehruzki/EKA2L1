import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.listing.ProgramContext;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.util.task.ConsoleTaskMonitor;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.math.BigInteger;

// Round 25: a stack scan at the moment !ecomserver first sends the connect message
// to !BackupServer found several candidate return addresses at 0x80780864,
// 0x8078086c, 0x80780870, 0x8078088c - all inside bafl.dll (Backup And File List
// utility library), not ecomserver.exe itself or a generic euser wrapper (both of
// which have shown up in every prior round instead). Force Thumb, run full
// auto-analysis (small file, cheap), dump every function found, and disassemble
// around each candidate address to see whether a "bl" call site immediately
// precedes it (confirming a genuine return address, not stack garbage).
public class DumpBaflOverview extends GhidraScript {
    private static final long[] CANDIDATES = { 0x80780864L, 0x8078086cL, 0x80780870L, 0x8078088cL };

    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/bafl_overview.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        ProgramContext ctx = currentProgram.getProgramContext();
        Register tmodeReg = ctx.getRegister("TMode");

        for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
            if (tmodeReg != null) {
                ctx.setValue(tmodeReg, b.getStart(), b.getEnd(), BigInteger.ONE);
            }
            disassemble(b.getStart());
        }
        analyzeAll(currentProgram);

        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        ConsoleTaskMonitor monitor2 = new ConsoleTaskMonitor();

        out.println("=== candidates: containing function + context ===");
        for (long t : CANDIDATES) {
            Address addr = toAddr(t);
            Function func = fm.getFunctionContaining(addr);
            out.printf("--- 0x%x: function=%s%n", t, func == null ? "none" : (func.getName() + " @ " + func.getEntryPoint()));

            Address winStart = addr.subtract(0x20);
            InstructionIterator ins = listing.getInstructions(winStart, true);
            int count = 0;
            while (ins.hasNext() && count < 20) {
                Instruction i = ins.next();
                if (i.getAddress().compareTo(addr.add(0x10)) > 0) break;
                out.printf("%s: %s%n", i.getAddress(), i.toString());
                count++;
            }
            out.println();
        }

        out.println("=== all functions ===");
        FunctionIterator fit = listing.getFunctions(true);
        int count = 0;
        while (fit.hasNext()) {
            Function func = fit.next();
            count++;
            out.printf("FUNC %s @ %s size=%d%n", func.getName(), func.getEntryPoint(), func.getBody().getNumAddresses());
        }
        out.println("TOTAL FUNCTIONS: " + count);

        out.close();
        println("WROTE " + outPath + " funcs=" + count);
    }
}
