import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.lang.Register;
import ghidra.program.model.lang.RegisterValue;
import ghidra.program.model.listing.ProgramContext;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.scalar.Scalar;
import ghidra.util.task.ConsoleTaskMonitor;

import java.io.FileInputStream;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.math.BigInteger;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.Map;
import java.util.Set;

// Builds ONE combined Ghidra program containing euser.dll + ecom.dll + ecomserver.exe
// code, each at its real ROM address, so cross-module BL/BLX references resolve
// properly. Then finds every call site targeting our known euser.dll wrapper
// functions and dumps the calling function's disassembly + decompiled pseudocode.
public class CombinedAnalysis extends GhidraScript {
    private static final long[] WATCH = {
        0x803aa9dcL, 0x803aaa76L, 0x803ac832L
    };

    private void addBlock(String name, String path, long addr, int size) throws Exception {
        Memory mem = currentProgram.getMemory();
        Address a = toAddr(addr);
        MemoryBlock block;
        try (FileInputStream fis = new FileInputStream(path)) {
            block = mem.createInitializedBlock(name, a, fis, size, monitor, false);
        }
        block.setRead(true);
        block.setWrite(false);
        block.setExecute(true);
    }

    private void setThumbEverywhere() {
        ProgramContext ctx = currentProgram.getProgramContext();
        Register reg = ctx.getRegister("TMode");
        if (reg == null) {
            println("WARNING: no TMode register");
            return;
        }
        for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
            try {
                ctx.setValue(reg, block.getStart(), block.getEnd(), BigInteger.ONE);
            } catch (Exception e) {
                println("TMode set failed for block " + block.getName() + ": " + e.getMessage());
            }
        }
    }

    @Override
    protected void run() throws Exception {
        addBlock("ecom.dll", "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/ecom_code.bin", 0x804C6608L, 5864);
        addBlock("ecomserver.exe", "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/ecomserver_code.bin", 0x804C7DC8L, 29820);

        setThumbEverywhere();

        println("Running full analysis over combined image...");
        analyzeAll(currentProgram);
        println("Analysis complete.");

        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/ghidra_combined_result.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();

        Set<Long> watchSet = new LinkedHashSet<>();
        for (long w : WATCH) watchSet.add(w);

        Map<String, Function> callerFuncs = new LinkedHashMap<>();

        InstructionIterator all = listing.getInstructions(true);
        int scanned = 0;
        while (all.hasNext()) {
            Instruction ins = all.next();
            scanned++;
            String mnem = ins.getMnemonicString();
            boolean isCall = mnem.toLowerCase().startsWith("bl");
            if (!isCall) continue;

            for (int i = 0; i < ins.getNumOperands(); i++) {
                for (Object o : ins.getOpObjects(i)) {
                    Long target = null;
                    if (o instanceof Address) target = ((Address) o).getOffset();
                    else if (o instanceof Scalar) target = ((Scalar) o).getUnsignedValue();
                    if (target != null && watchSet.contains(target)) {
                        out.printf("CALL at %s -> 0x%x   (%s)%n", ins.getAddress(), target, ins.toString());
                        Function f = fm.getFunctionContaining(ins.getAddress());
                        if (f != null) {
                            callerFuncs.putIfAbsent(f.getEntryPoint().toString(), f);
                        }
                    }
                }
            }
        }
        out.printf("scanned %d instructions total, %d unique caller functions found%n%n", scanned, callerFuncs.size());

        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        ConsoleTaskMonitor monitor2 = new ConsoleTaskMonitor();

        for (Map.Entry<String, Function> e : callerFuncs.entrySet()) {
            Function func = e.getValue();
            out.println("=========================================================");
            out.printf("FUNCTION %s @ %s (in block containing this addr)%n", func.getName(), func.getEntryPoint());
            out.println("=========================================================\n");

            out.println("--- disassembly ---");
            InstructionIterator ins2 = listing.getInstructions(func.getBody(), true);
            while (ins2.hasNext()) {
                Instruction i = ins2.next();
                out.printf("%s: %s%n", i.getAddress(), i.toString());
            }

            out.println("\n--- decompiled pseudocode ---");
            DecompileResults res = decomp.decompileFunction(func, 60, monitor2);
            if (res.decompileCompleted()) {
                out.println(res.getDecompiledFunction().getC());
            } else {
                out.println("decompile failed: " + res.getErrorMessage());
            }
            out.println("\n");
        }

        out.close();
        println("WROTE RESULT TO " + outPath);
    }
}
