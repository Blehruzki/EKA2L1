import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.listing.ProgramContext;
import ghidra.util.task.ConsoleTaskMonitor;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.math.BigInteger;

// Live single-step trace showed eikbackupsrv.dll last executing at 0x8214722c
// (called from euser.dll at 0x8039f7dc) right before !BackupServer goes
// permanently idle. Find out what this function actually does.
public class DumpFinalHandler extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/final_handler_result.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        ConsoleTaskMonitor monitor2 = new ConsoleTaskMonitor();

        ProgramContext ctx = currentProgram.getProgramContext();
        Register tmodeReg = ctx.getRegister("TMode");

        long target = 0x8214722cL;
        Address addr = toAddr(target);
        Function func = fm.getFunctionContaining(addr);
        if (func == null) {
            try {
                clearListing(addr.subtract(0x20), addr.add(0x100));
                if (tmodeReg != null) {
                    ctx.setValue(tmodeReg, addr.subtract(0x20), addr.add(0x100), BigInteger.ONE);
                }
                disassemble(addr.subtract(0x20));
                createFunction(addr.subtract(0x20), "final_handler");
                func = fm.getFunctionContaining(addr);
            } catch (Exception e) {
                out.println("force disasm from -0x20 failed: " + e);
            }
        }
        if (func == null) {
            try {
                clearListing(addr, addr.add(0x100));
                if (tmodeReg != null) {
                    ctx.setValue(tmodeReg, addr, addr.add(0x100), BigInteger.ONE);
                }
                disassemble(addr);
                createFunction(addr, "final_handler_exact");
                func = fm.getFunctionContaining(addr);
            } catch (Exception e) {
                out.println("force disasm at exact addr failed: " + e);
            }
        }

        out.printf("=== target 0x%x ===%n", target);
        if (func == null) {
            out.println("  still no containing function found");
        } else {
            out.printf("FUNCTION %s @ %s size=%d%n", func.getName(), func.getEntryPoint(), func.getBody().getNumAddresses());
            out.println("--- disassembly ---");
            InstructionIterator ins = listing.getInstructions(func.getBody(), true);
            while (ins.hasNext()) {
                Instruction i = ins.next();
                out.printf("%s: %s%n", i.getAddress(), i.toString());
            }
            out.println("--- decompiled ---");
            DecompileResults res = decomp.decompileFunction(func, 30, monitor2);
            if (res.decompileCompleted()) {
                out.println(res.getDecompiledFunction().getC());
            } else {
                out.println("decompile failed: " + res.getErrorMessage());
            }
        }

        out.close();
        println("WROTE " + outPath);
    }
}
