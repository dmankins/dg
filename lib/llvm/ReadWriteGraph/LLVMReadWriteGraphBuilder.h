#ifndef LLVM_DG_RWG_BUILDER_H
#define LLVM_DG_RWG_BUILDER_H

#include <unordered_map>
#include <memory>

// ignore unused parameters in LLVM libraries
#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <llvm/Support/raw_os_ostream.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Constants.h>

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include "dg/ReadWriteGraph/ReadWriteGraph.h"
#include "dg/llvm/PointerAnalysis/PointerAnalysis.h"
#include "dg/llvm/DataDependence/LLVMDataDependenceAnalysisOptions.h"

#ifndef NDEBUG
#include "dg/util/debug.h"
#endif // NDEBUG

namespace dg {
namespace dda {

template <typename NodeT>
class NodesSeq {
    // we can optimize this later...
    std::vector<NodeT*> nodes;
    NodeT *representant{nullptr};

public:
    NodesSeq(const std::initializer_list<NodeT*>& lst) {
        if (lst.size() > 0) {
            nodes.insert(nodes.end(), lst.begin(), lst.end());
            representant = *lst.begin();
        }
    }

    NodeT *setRepresentant(NodeT *r) {
        representant = r;
    }

    NodeT *getRepresentant() const {
        return representant;
    }

    auto begin() -> decltype(nodes.begin()) { return nodes.begin(); }
    auto end() -> decltype(nodes.end()) { return nodes.end(); }
    auto begin() const -> decltype(nodes.begin()) { return nodes.begin(); }
    auto end() const -> decltype(nodes.end()) { return nodes.end(); }
};
 

template <typename NodeT, typename BBlockT, typename SubgraphT>
class GraphBuilder {
    using NodesMappingT = std::unordered_map<const llvm::Value *, NodeT *>;

    const llvm::Module *_module;

    /*
    struct Subgraph {
        SubgraphT *subgraph;
        Subgraph(const SubgraphT&) = delete;
    };
    */

    std::vector<SubgraphT> _subgraphs;
    NodesMappingT _nodes;

    void buildCFG(const llvm::Function& F, SubgraphT& subg) {
    }

    void buildICFG() {
        DBG_SECTION_BEGIN(rwg, "Building call edges");
        DBG_SECTION_END(rwg, "Building call edges done");
    }

    BBlockT& buildBBlock(const llvm::BasicBlock& B, SubgraphT& subg) {
        DBG_SECTION_BEGIN(rwg, "Building basic block");
        auto& bblock = createBBlock(&B, subg);

        for (auto& I : B) {
            assert(_nodes.find(&I) == _nodes.end()
                    && "Building a node that we already have");

            llvm::errs() << "Building " << I << "\n";
            const auto& nds = createNode(&I);
            for (auto *node : nds) {
                bblock.append(node);
            }

            _nodes[&I] = nds.getRepresentant();
        }

        DBG_SECTION_END(rwg, "Building basic block done");
        return bblock;
    }

    void buildSubgraph(const llvm::Function& F) {
        DBG_SECTION_BEGIN(rwg, "Building the subgraph for " << F.getName().str());
        auto& subg = createSubgraph(&F);

        DBG(rwg, "Building basic blocks");
        for (auto& B : F) {
            auto& bblock = buildBBlock(B, subg);
        }

        DBG(rwg, "Building CFG");
        buildCFG(F, subg);

        DBG_SECTION_END(rwg, "Building the subgraph done");
    }

public:
    GraphBuilder(const llvm::Module *m) : _module(m) {}
    virtual ~GraphBuilder() = default;

    const llvm::Module *getModule() const { return _module; }
    const llvm::DataLayout *getDataLayout() const { return &_module->getDataLayout(); }

    const NodesMappingT& getNodesMapping() const {
        return _nodes;
    }

    NodeT *getNode(const llvm::Value *v) {
        auto it = _nodes.find(v);
        return it == _nodes.end() ? nullptr : it->second;
    }

    const NodeT *getNode(const llvm::Value *v) const {
        auto it = _nodes.find(v);
        return it == _nodes.end() ? nullptr : it->second;
    }

    //virtual NodeT& getOperand(const llvm::Value *);
    virtual NodesSeq<NodeT> createNode(const llvm::Value *) = 0;
    virtual BBlockT& createBBlock(const llvm::BasicBlock *, SubgraphT&) = 0;
    virtual SubgraphT& createSubgraph(const llvm::Function *) = 0;

    void buildFromLLVM() {
        assert(_module && "Do not have the LLVM module");

        // build only reachable calls from CallGraph
        // (if given as an argument)
        // FIXME: do a walk on reachable blocks so that
        // we respect the domination properties of instructions
        for (auto& F : *_module) {
            buildSubgraph(F);
        }

        // add call-edges
        buildICFG();

        //buildGlobals();
    }
};


class LLVMReadWriteGraphBuilder : public GraphBuilder<RWNode, RWBBlock, RWSubgraph> {
    const LLVMDataDependenceAnalysisOptions& _options;
    // points-to information
    dg::LLVMPointerAnalysis *PTA;
    // even the data-flow analysis needs uses to have the mapping of llvm values
    bool buildUses{true};
    // optimization for reaching-definitions analysis
    // TODO: do not do this while building graph, but let the analysis
    // modify the graph itself (or forget it some other way as we'll
    // have the interprocedural graph)
    // bool forgetLocalsAtReturn{false};

    ReadWriteGraph graph;

    //RWNode& getOperand(const llvm::Value *) override;
    NodesSeq<RWNode> createNode(const llvm::Value *) override;
    RWBBlock& createBBlock(const llvm::BasicBlock *, RWSubgraph& subg) override {
        return subg.createBBlock();
    }

    RWSubgraph& createSubgraph(const llvm::Function *) override {
        return graph.createSubgraph();
    }

    /*
    struct Subgraph {
        std::map<const llvm::BasicBlock *, Block> blocks;

        Block& createBlock(const llvm::BasicBlock *b) {
            auto it = blocks.emplace(b, Block());
            assert(it.second && "Already had this block");

            return it.first->second;
        }
        Block *entry{nullptr};
        std::vector<RWNode *> returns;

        RWSubgraph *rwsubgraph;
    };

    // map of all nodes we created - use to look up operands
    std::unordered_map<const llvm::Value *, RWNode *> nodes_map;

    std::map<const llvm::CallInst *, RWNode *> threadCreateCalls;
    std::map<const llvm::CallInst *, RWNode *> threadJoinCalls;

    // mapping of call nodes to called subgraphs
    std::map<std::pair<RWNode *, RWNode *>, std::set<Subgraph *>> calls;

    // map of all built subgraphs - the value type is a pair (root, return)
    std::unordered_map<const llvm::Value *, Subgraph> subgraphs_map;


    */

    RWNode& create(RWNodeType t) { return graph.create(t); }

public:
    LLVMReadWriteGraphBuilder(const llvm::Module *m,
                              dg::LLVMPointerAnalysis *p,
                              const LLVMDataDependenceAnalysisOptions& opts)
        : GraphBuilder(m), _options(opts), PTA(p) {}

    ReadWriteGraph&& build() {
        buildFromLLVM();
        return std::move(graph);
    }

    RWNode *getOperand(const llvm::Value *val);

    /*

    // let the user get the nodes map, so that we can
    // map the points-to informatio back to LLVM nodes
    const std::unordered_map<const llvm::Value *, RWNode *>&
                                getNodesMap() const { return nodes_map; }

    RWNode *getNode(const llvm::Value *val) {
        auto it = nodes_map.find(val);
        if (it == nodes_map.end())
            return nullptr;

        return it->second;
    }


private:

    static void blockAddSuccessors(Subgraph& subg, Block& block,
                                   const llvm::BasicBlock *llvmBlock,
                                   std::set<const llvm::BasicBlock *>& visited);

*/
    std::vector<DefSite> mapPointers(const llvm::Value *where,
                                     const llvm::Value *val,
                                     Offset size);

    RWNode *createStore(const llvm::Instruction *Inst);
    RWNode *createLoad(const llvm::Instruction *Inst);
    RWNode *createAlloc(const llvm::Instruction *Inst);
    RWNode *createDynAlloc(const llvm::Instruction *Inst, AllocationFunction type);
    RWNode *createRealloc(const llvm::Instruction *Inst);
    RWNode *createReturn(const llvm::Instruction *Inst);



/*

    void addNode(const llvm::Value *val, RWNode *node)
    {
        auto it = nodes_map.find(val);
        assert(it == nodes_map.end() && "Adding a node that we already have");

        nodes_map.emplace_hint(it, val, node);
        node->setUserData(const_cast<llvm::Value *>(val));
    }

    // FIXME: rename this method
    void addArtificialNode(const llvm::Value *val, RWNode *node)
    {
        node->setUserData(const_cast<llvm::Value *>(val));
    }

    RWNode *funcFromModel(const FunctionModel *model, const llvm::CallInst *);
    Block& buildBlock(Subgraph& subg, const llvm::BasicBlock& block);
    Block& buildBlockNodes(Subgraph& subg, const llvm::BasicBlock& block);
    Subgraph& buildFunction(const llvm::Function& F);
    Subgraph *getOrCreateSubgraph(const llvm::Function *F);

    std::pair<RWNode *, RWNode *> buildGlobals();

    std::pair<RWNode *, RWNode *>
    createCallToFunction(const llvm::Function *F, const llvm::CallInst *CInst);

    std::pair<RWNode *, RWNode *>
    createCall(const llvm::Instruction *Inst);

    RWNode * createCallToZeroSizeFunction(const llvm::Function *function,
                                         const llvm::CallInst *CInst);

    std::pair<RWNode *, RWNode *>
    createCallToFunctions(const std::vector<const llvm::Function *> &functions,
                          const llvm::CallInst *CInst);

    RWNode * createPthreadCreateCalls(const llvm::CallInst *CInst);
    RWNode * createPthreadJoinCall(const llvm::CallInst *CInst);
    RWNode * createPthreadExitCall(const llvm::CallInst *CInst);

    RWNode *createIntrinsicCall(const llvm::CallInst *CInst);
    RWNode *createUndefinedCall(const llvm::CallInst *CInst);

    bool isInlineAsm(const llvm::Instruction *instruction);

    void matchForksAndJoins();
    */
};

} // namespace dda
} // namespace dg

#endif
