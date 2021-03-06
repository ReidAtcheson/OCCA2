#ifndef OCCA_PARSER_HEADER
#define OCCA_PARSER_HEADER

#include "occaParserDefines.hpp"
#include "occaParserMacro.hpp"
#include "occaParserTools.hpp"
#include "occaParserNodes.hpp"
#include "occaParserTypes.hpp"
#include "occaParserStatement.hpp"
#include "occaTools.hpp"

namespace occa {
  namespace parserNS {
    class occaLoopInfo;

    class parserBase {
    public:
      static const int version = 100;

      bool parsingC;

      macroMap_t macroMap;
      std::vector<macroInfo> macros;

      bool macrosAreInitialized;

      varUsedMap_t varUpdateMap;
      varUsedMap_t varUsedMap;     // Statements are placed backwards

      kernelInfoMap_t kernelInfoMap;

      statement *globalScope;

      //---[ Warnings ]-----------------
      bool warnForMissingBarriers;
      bool warnForBarrierConditionals;
      //================================

      parserBase();

      const std::string parseFile(const std::string &filename,
                                  const bool parsingC_ = true);

      const std::string parseSource(const char *cRoot);

      //---[ Macro Parser Functions ]---
      std::string getMacroName(const char *&c);

      bool evaluateMacroStatement(const char *&c);
      static typeHolder evaluateLabelNode(strNode *labelNodeRoot);

      void loadMacroInfo(macroInfo &info, const char *&c);
      int loadMacro(const std::string &line, const int state = doNothing);

      void applyMacros(std::string &line);

      strNode* preprocessMacros(strNode *nodeRoot);

      strNode* splitAndPreprocessContent(const std::string &s);
      strNode* splitAndPreprocessContent(const char *cRoot);
      strNode* splitAndPreprocessFortranContent(const char *cRoot);
      //====================================

      void initMacros(const bool parsingC = true);
      void initFortranMacros();

      void loadLanguageTypes();

      void applyToAllStatements(statement &s,
                                applyToAllStatements_t func);

      void applyToStatementsDefiningVar(applyToStatementsDefiningVar_t func);

      void applyToStatementsUsingVar(varInfo &info,
                                     applyToStatementsUsingVar_t func);

      bool statementIsAKernel(statement &s);

      statement* getStatementKernel(statement &s);
      statement* getStatementOuterMostLoop(statement &s);

      bool statementKernelUsesNativeOCCA(statement &s);

      bool statementKernelUsesNativeOKL(statement &s);

      bool statementKernelUsesNativeLanguage(statement &s);

      void addOccaForCounter(statement &s,
                             const std::string &ioLoop,
                             const std::string &loopNest,
                             const std::string &loopIters = "");

      void setupOccaFors(statement &s);

      bool statementHasOccaOuterFor(statement &s);
      bool statementHasOccaFor(statement &s);

      bool statementHasOklFor(statement &s);

      bool statementHasOccaStuff(statement &s);

      void markKernelFunctions(statement &s);

      void labelNativeKernels();

      void setupCudaVariables(statement &s);

      void addFunctionPrototypes();

      int statementOccaForNest(statement &s);
      bool statementIsAnOccaFor(statement &s);

      void addOccaBarriers();
      void addOccaBarriersToStatement(statement &s);

      bool statementHasBarrier(statement &s);

      void fixOccaForStatementOrder(statement &origin, statementNode *sn);
      void fixOccaForOrder();

      void addParallelFors(statement &s);

      void updateConstToConstant();

      strNode* occaExclusiveStrNode(varInfo &info,
                                    const int depth,
                                    const int sideDepth);

      void addArgQualifiers();

      void modifyExclusiveVariables(statement &s);

      void modifyTextureVariables();

      statementNode* splitKernelStatement(statementNode *snKernel,
                                          kernelInfo &info);

      statementNode* getOuterLoopsInStatement(statement &s);
      statementNode* getOccaLoopsInStatement(statement &s,
                                             const bool getNestedLoops = true);

      int kernelCountInOccaLoops(statementNode *occaLoops);

      void zeroOccaIdsFrom(statement &s);

      statementNode* createNestedKernelsFromLoops(statementNode *snKernel,
                                                  kernelInfo &info,
                                                  statementNode *outerLoopRoot);

      std::string getNestedKernelArgsFromLoops(statement &sKernel);

      void setupHostKernelArgsFromLoops(statement &sKernel);

      void loadKernelInfos();

      void stripOccaFromKernel(statement &s);

      std::string occaScope(statement &s);

      void incrementDepth(statement &s);

      void decrementDepth(statement &s);

      statementNode* findStatementWith(statement &s,
                                       findStatementWith_t func);

      static int getKernelOuterDim(statement &s);
      static int getKernelInnerDim(statement &s);

      int getOuterMostForDim(statement &s);
      int getInnerMostForDim(statement &s);
      int getForDim(statement &s, const std::string &tag);

      void checkPathForConditionals(statementNode *path);

      int findLoopSections(statement &s,
                           statementNode *path,
                           statementIdMap_t &loopSection,
                           int section = 0);

      bool varInTwoSegments(varInfo &var,
                            statementIdMap_t &loopSection);

      varInfoNode* findVarsMovingToTop(statement &s,
                                       statementIdMap_t &loopSection);

      void splitDefineForVariable(statement &origin,
                                  varInfo &var);

      void addInnerForsToStatement(statement &s,
                                   const int innerDim);

      statementNode* addInnerForsBetweenBarriers(statement &origin,
                                                 statementNode *includeStart,
                                                 const int innerDim);

      void addInnerFors(statement &s);
      void addOuterFors(statement &s);

      void removeUnnecessaryBlocksInKernel(statement &s);
      void floatSharedVarsInKernel(statement &s);
      void addOccaForsToKernel(statement &s);

      void addOccaFors();

      void setupOccaVariables(statement &s);
    };

    bool isAnOccaID(const std::string &s);
    bool isAnOccaInnerID(const std::string &s);
    bool isAnOccaOuterID(const std::string &s);
    bool isAnOccaGlobalID(const std::string &s);

    bool isAnOccaDim(const std::string &s);
    bool isAnOccaInnerDim(const std::string &s);
    bool isAnOccaOuterDim(const std::string &s);
    bool isAnOccaGlobalDim(const std::string &s);

    strNode* splitContent(const std::string &str, const bool parsingC = true);
    strNode* splitContent(const char *cRoot, const bool parsingC = true);

    bool checkWithLeft(strNode *nodePos,
                       const std::string &leftValue,
                       const std::string &rightValue,
                       const bool parsingC = true);

    void mergeNodeWithLeft(strNode *&nodePos,
                           const bool addSpace = true,
                           const bool parsingC = true);

    strNode* labelCode(strNode *lineNodeRoot, const bool parsingC = true);

    void initKeywords(const bool parsingC = true);
    void initFortranKeywords();

    //---[ OCCA Loop Info ]-------------
    class occaLoopInfo {
    public:
      statement *sInfo;
      bool parsingC;

      occaLoopInfo(statement &s,
                   const bool parsingC_,
                   const std::string &tag = "");

      void lookForLoopFrom(statement &s,
                           const std::string &tag = "");

      void loadForLoopInfo(int &innerDims, int &outerDims,
                           std::string *innerIters,
                           std::string *outerIters);

      void getLoopInfo(std::string &ioLoopVar,
                       std::string &ioLoop,
                       std::string &loopNest);

      void getLoopNode1Info(std::string &iter,
                            std::string &start);

      void getLoopNode2Info(std::string &bound,
                            std::string &iterCheck);

      void getLoopNode3Info(std::string &stride,
                            std::string &strideOpSign,
                            std::string &strideOp);

      void setIterDefaultValues();

      std::string getSetupExpression();
    };
    //==================================
    //==============================================
  };

  // Just to ignore the namespace
  class parser : public parserNS::parserBase {};
};

#endif
