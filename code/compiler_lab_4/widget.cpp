#include "widget.h"
#include "ui_widget.h"
#include <QString>
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QFileDialog>
#include <QTextCodec>
#include <QMessageBox>
#include <iostream>
#include <map>
#include <vector>
#include <stack>
#include <unordered_map>
#include <queue>
#include <set>
#include <unordered_set>
#include <algorithm>
#include <string>
#include <sstream>
#include <fstream>
#pragma execution_character_set("utf-8")
using namespace std;

Widget::Widget(QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
{
    ui->setupUi(this);
}

Widget::~Widget()
{
    delete ui;
}

// 全局文法变量
string grammarStr;

// 结构化后的文法map
unordered_map<char, set<string>> grammarMap;

// 文法unit（用于LR0）
struct grammarUnit
{
    int gid;
    char left;
    string right;
    grammarUnit(char l, string r)
    {
        left = l;
        right = r;
    }
};

// 文法数组（用于LR0）
deque<grammarUnit> grammarDeque;

// LR0结果提示字符串
QString LR0Result;

// 文法查找下标
map<pair<char, string>, int> grammarToInt;


/*************  公用函数 ****************/
// 非终结符
bool isBigAlpha(char c)
{
    return c >= 'A' && c <= 'Z';
}

// 终结符
bool isSmallAlpha(char c)
{
    return !(c >= 'A' && c <= 'Z') && c != '@';
}
void reset();

// 开始符号
char startSymbol;

// 增广后开始符号
char trueStartSymbol;

/************* 文法初始化处理 ****************/

// 处理文法
void handleGrammar()
{
    vector<string> lines;
    istringstream iss(grammarStr);
    string line;

    // 防止中间有换行符
    while (getline(iss, line))
    {
        if (!line.empty())
        {
            lines.push_back(line);
        }
    }

    for (const auto& rule : lines)
    {
        istringstream ruleStream(rule);
        char nonTerminal;
        ruleStream >> nonTerminal;  // 读取非终结符

        // 验证非终结符的格式
        if (!isBigAlpha(nonTerminal))
        {
            QMessageBox::critical(nullptr, "Error", "文法开头必须是非终结符（大写字母）!");
            continue;
        }

        // 跳过箭头及空格
        ruleStream.ignore(numeric_limits<streamsize>::max(), '-');
        ruleStream.ignore(numeric_limits<streamsize>::max(), '>');

        string rightHandSide;
        ruleStream >> rightHandSide;  // 获取产生式右侧

        // 如果是第一条规则，则认为是开始符号
        if (grammarMap.empty())
        {
            startSymbol = nonTerminal;
            trueStartSymbol = startSymbol;
        }

        // 将文法结构化
        grammarMap[nonTerminal].insert(rightHandSide);

        // 为LR0做准备
        grammarDeque.push_back(grammarUnit(nonTerminal, rightHandSide));
    }

    // 增广处理
    if (grammarMap[startSymbol].size() > 1)
    {
        // 如果开始符号多于2个，说明需要增广，为了避免出现字母重复，采用^作为增广后的字母，后期输出特殊处理
        grammarDeque.push_front(grammarUnit('^', string(1, startSymbol)));
        LR0Result += QString::fromStdString("进行了增广处理\n");
        trueStartSymbol = '^';
    }

    // 开始编号
    int gid = 0;
    for (auto& g : grammarDeque)
    {
        g.gid = gid++;
        LR0Result += QString::number(g.gid) + QString::fromStdString(":") 
            + QString::fromStdString(g.left == '^' ? "E\'" : string(1, g.left)) + QString::fromStdString("->")
            + QString::fromStdString(g.right) + "\n";
        // 存入map中
        grammarToInt[make_pair(g.left, g.right)] = g.gid;
    }

}

/************* First集合求解 ****************/


// First集合单元
struct firstUnit
{
    set<char> s;
    bool isEpsilon = false;
};

// 非终结符的First集合
map<char, firstUnit> firstSets;

// 计算First集合
bool calculateFirstSets()
{
    bool flag = false;
    for (auto& grammar : grammarMap)
    {
        char nonTerminal = grammar.first;
        // 保存当前First集合的大小，用于检查是否有变化
        size_t originalSize = firstSets[nonTerminal].s.size();
        bool originalE = firstSets[nonTerminal].isEpsilon;
        for (auto& g : grammar.second)
        {
            int k = 0;
            while (k <= g.size() - 1)
            {
                set<char> first_k;
                if (g[k] == '@')
                {
                    k++;
                    continue;
                }
                else if (isSmallAlpha(g[k]))
                {
                    first_k.insert(g[k]);
                }
                else
                {
                    first_k = firstSets[g[k]].s;
                }
                firstSets[nonTerminal].s.insert(first_k.begin(), first_k.end());
                // 如果是终结符或者没有空串在非终结符中，直接跳出
                if (isSmallAlpha(g[k]) || !firstSets[g[k]].isEpsilon)
                {
                    break;
                }
                k++;
            }
            if (k == g.size())
            {
                firstSets[nonTerminal].isEpsilon = true;
            }
        }
        // 看原始大小和是否变化epsilon，如果变化说明还得重新再来一次
        if (originalSize != firstSets[nonTerminal].s.size() || originalE != firstSets[nonTerminal].isEpsilon)
        {
            flag = true;
        }
    }
    return flag;
}

void getFirstSets()
{
    // 不停迭代，直到First集合不再变化
    bool flag = false;
    do
    {
        flag = calculateFirstSets();
    } while (flag);
}


/************* Follow集合求解 ****************/
// Follow集合单元
struct followUnit
{
    set<char> s;
};

// 非终结符的Follow集合
map<char, followUnit> followSets;

// 添加Follow集合
void addToFollow(char nonTerminal, const set<char>& elements)
{
    followSets[nonTerminal].s.insert(elements.begin(), elements.end());
}

// 计算Follow集合
bool calculateFollowSets()
{
    bool flag = false;
    for (auto& grammar : grammarMap)
    {
        char nonTerminal = grammar.first;

        for (auto& g : grammar.second)
        {
            for (int i = 0; i < g.size(); ++i)
            {
                if (isSmallAlpha(g[i]) || g[i] == '@')
                {
                    continue;  // 跳过终结符
                }

                set<char> follow_k;
                size_t originalSize = followSets[g[i]].s.size();

                if (i == g.size() - 1)
                {
                    // Case A: A -> αB, add Follow(A) to Follow(B)
                    follow_k.insert(followSets[nonTerminal].s.begin(), followSets[nonTerminal].s.end());
                }
                else
                {
                    // Case B: A -> αBβ
                    int j = i + 1;
                    while (j < g.size())
                    {
                        if (isSmallAlpha(g[j]))
                        {   // 终结符直接加入并跳出
                            follow_k.insert(g[j]);
                            break;
                        }
                        else
                        {   // 非终结符加入first集合
                            set<char> first_beta = firstSets[g[j]].s;
                            follow_k.insert(first_beta.begin(), first_beta.end());

                            // 如果没有空串在first集合中，停止。
                            if (!firstSets[g[j]].isEpsilon)
                            {
                                break;
                            }

                            ++j;
                        }

                    }

                    // If β is ε or β is all nullable, add Follow(A) to Follow(B)
                    if (j == g.size())
                    {
                        follow_k.insert(followSets[nonTerminal].s.begin(), followSets[nonTerminal].s.end());
                    }
                }

                addToFollow(g[i], follow_k);
                // 检查是否变化
                if (originalSize != followSets[g[i]].s.size())
                {
                    flag = true;
                }
            }
        }


    }

    return flag;
}

void getFollowSets()
{
    // 开始符号加入$
    addToFollow(startSymbol, { '$' });

    // 不停迭代，直到Follow集合不再变化
    bool flag = false;
    do
    {
        flag = calculateFollowSets();
    } while (flag);
}

/************* LR0 DFA表生成 ****************/

// 状态编号
int scnt = 0;
// 项目编号
int ccnt = 0;

// DFA表每一项项目的结构
struct dfaCell
{
    int cellid; // 这一项的编号，便于后续判断状态相同
    int gid; // 文法编号
    int index = 0; // .在第几位，如i=3, xxx.x，i=0, .xxxx, i=4, xxxx
};

// 用于通过编号快速找到对应结构
vector<dfaCell> dfaCellVector;

struct nextStateUnit
{
    char c; // 通过什么字符进入这个状态
    int sid; // 下一个状态id是什么
};

// DFA表状态
struct dfaState
{
    int sid; // 状态id
    vector<int> originV;    // 未闭包前的cell
    vector<int> cellV;  // 存储这个状态的cellid
    bool isEnd = false; // 是否为规约状态
    vector<nextStateUnit> nextStateVector; // 下一个状态集合
    set<char> right_VNs; // 判断是否已经处理过这个非终结符
};

// 用于通过编号快速找到对应结构
vector<dfaState> dfaStateVector;

// 非终结符集合
set<char> VN;
// 终结符集合
set<char> VT;

// 判断是不是新结构
int isNewCell(int gid, int index)
{
    for (const dfaCell& cell : dfaCellVector)
    {
        // 检查dfaCellVector中是否存在相同的gid和index的dfaCell
        if (cell.gid == gid && cell.index == index)
        {
            return cell.cellid; // 不是新结构
        }
    }
    return -1; // 是新结构
}

// 判断是不是新状态
int isNewState(const vector<int>& cellIds)
{
    for (const dfaState& state : dfaStateVector)
    {
        // 检查状态中的originV是否相同
        if (state.originV.size() == cellIds.size() &&
            equal(state.originV.begin(), state.originV.end(), cellIds.begin()))
        {
            return state.sid; // 不是新状态
        }
    }

    return -1; // 是新状态
}

// DFS标记数组
set<int> visitedStates;

// 创建LR0的初始状态
void createFirstState()
{
    // 由于增广，一定会只有一个入口
    dfaState zero = dfaState();
    zero.sid = scnt++; // 给他一个id
    dfaStateVector.push_back(zero); // 放入数组中

    // 添加初始的LR0项，即E' -> .S
    dfaCell startCell;
    startCell.gid = 0; // 这里假设增广文法的编号为0
    startCell.index = 0;
    startCell.cellid = ccnt++;

    dfaCellVector.push_back(startCell);

    // 把初始LR0项放入初始状态
    dfaStateVector[0].cellV.push_back(startCell.cellid);
    dfaStateVector[0].originV.push_back(startCell.cellid);
}

// 生成LR0状态
void generateLR0State(int stateId)
{
    // DFS,如果走过就不走了
    if (visitedStates.count(stateId) > 0) {
        return;
    }

    // 标记走过了
    visitedStates.insert(stateId);

    //if (dfaStateVector[stateId].isEnd)
    //{
    //    return;
    //}

    qDebug() << stateId << endl;

    // 求闭包
    for (int i = 0; i < dfaStateVector[stateId].cellV.size(); ++i)
    {
        dfaCell& currentCell = dfaCellVector[dfaStateVector[stateId].cellV[i]];

        qDebug() << grammarDeque[currentCell.gid].left << QString::fromStdString("->") << QString::fromStdString( grammarDeque[currentCell.gid].right) << endl;

        qDebug() << "current index:" << currentCell.index << endl;

        // 如果点号在产生式末尾或者空串，则跳过（LR0不需要结束）
        if (currentCell.index == grammarDeque[currentCell.gid].right.length() || grammarDeque[currentCell.gid].right == "@")
        {
            dfaStateVector[stateId].isEnd = true;
            continue;
        }

        char nextSymbol = grammarDeque[currentCell.gid].right[currentCell.index];
        

        // 如果nextSymbol是非终结符，则将新项添加到状态中
        if (isBigAlpha(nextSymbol) && dfaStateVector[stateId].right_VNs.find(nextSymbol) == dfaStateVector[stateId].right_VNs.end())
        {
            dfaStateVector[stateId].right_VNs.insert(nextSymbol);
            for (auto& grammar : grammarMap[nextSymbol])
            {
                // 获取通过nextSymbol转移的新LR0项
                dfaCell nextCell = dfaCell();
                nextCell.gid = grammarToInt[make_pair(nextSymbol,grammar)];
                nextCell.index = 0;
                int nextcellid = isNewCell(nextCell.gid, nextCell.index);
                if (nextcellid == -1)
                {
                    nextCell.cellid = ccnt++;
                    dfaCellVector.push_back(nextCell);
                    dfaStateVector[stateId].cellV.push_back(nextCell.cellid);
                }
                else dfaStateVector[stateId].cellV.push_back(nextcellid);
            }
        
        }
    }

    // 暂存新状态
    map<char,dfaState> tempSave;
    // 生成新状态，但还不能直接存到dfaStateVector中，我们要校验他是否和之前的状态一样
    for (int i = 0; i < dfaStateVector[stateId].cellV.size(); ++i)
    {
        dfaCell& currentCell = dfaCellVector[dfaStateVector[stateId].cellV[i]];

        qDebug() << grammarDeque[currentCell.gid].left << QString::fromStdString("->") << QString::fromStdString(grammarDeque[currentCell.gid].right) << endl;

        qDebug() << "current index:" << currentCell.index << endl;

        // 如果点号在产生式末尾，则跳过（LR0不需要结束）
        if (currentCell.index == grammarDeque[currentCell.gid].right.length() || grammarDeque[currentCell.gid].right == "@")
        {
            continue;
        }

        // 下一个字符
        char nextSymbol = grammarDeque[currentCell.gid].right[currentCell.index];

        // 创建下一个状态（临时的）
        dfaState& nextState = tempSave[nextSymbol];
        dfaCell nextStateCell = dfaCell();
        nextStateCell.gid = currentCell.gid;
        nextStateCell.index = currentCell.index + 1;

        // 看看里面的项目是否有重复的，如果重复拿之前的就好，不重复生成
        int nextStateCellid = isNewCell(nextStateCell.gid, nextStateCell.index);
        if (nextStateCellid == -1)
        {
            nextStateCell.cellid = ccnt++;
            dfaCellVector.push_back(nextStateCell);
        }
        else nextStateCell.cellid = nextStateCellid;
        nextState.cellV.push_back(nextStateCell.cellid);
        nextState.originV.push_back(nextStateCell.cellid);

        // 收集一下，方便后面画表
        if (isBigAlpha(nextSymbol))
        {
            VN.insert(nextSymbol);
        }
        else if (isSmallAlpha(nextSymbol))
        {
            VT.insert(nextSymbol);
        }
    }

    // 校验状态是否有重复的
    for (auto& t : tempSave)
    {
        dfaState nextState = dfaState();
        int newStateId = isNewState(t.second.originV);
        // 不重复就新开一个状态
        if (newStateId == -1)
        {
            nextState.sid = scnt++;
            nextState.cellV = t.second.cellV;
            nextState.originV = t.second.originV;
            dfaStateVector.push_back(nextState);
        }
        else nextState.sid = newStateId;
        // 存入现在这个状态的nextStateVector
        nextStateUnit n = nextStateUnit();
        n.sid = nextState.sid;
        n.c = t.first;
        dfaStateVector[stateId].nextStateVector.push_back(n);
    }

    // 对每个下一个状态进行递归
    int nsize = dfaStateVector[stateId].nextStateVector.size();
    for (int i = 0; i < nsize; i++)
    {
        auto& nextunit = dfaStateVector[stateId].nextStateVector[i];
        generateLR0State(nextunit.sid);
    }
}

// 生成LR0入口
void getLR0()
{
    visitedStates.clear();

    // 首先生成第一个状态
    createFirstState();

    // 递归生成其他状态
    generateLR0State(0);
}

// 拼接字符串，获取状态内的文法
string getStateGrammar(const dfaState& d)
{
    string result = "";
    for (auto cell : d.cellV)
    {
        const dfaCell& dfaCell = dfaCellVector[cell];
        // 拿到文法
        int gid = dfaCell.gid;
        grammarUnit g = grammarDeque[gid];
        // 拿到位置
        int i = dfaCell.index;
        // 拼接结果
        string r = "";
        r += g.left == '^' ? "E\'->" : string(1, g.left) + "->";
        string right = g.right == "@" ? "" : g.right;
        right.insert(i, 1, '.');
        r += right;
        result += r + " ";
    }
    return result;
}

/******************** SLR1分析 ***************************/
// 检查移进-规约冲突
bool SLR1Fun1()
{
    for (const dfaState& state : dfaStateVector)
    {
        // 规约项目的左边集合
        set<char> a;
        // 终结符
        set<char> rVT;
        // 不是规约状态不考虑
        if (!state.isEnd) continue;
        // 规约状态
        for (int cellid : state.cellV)
        {
            // 拿到这个cell
            const dfaCell& cell = dfaCellVector[cellid];
            // 获取文法
            const grammarUnit gm = grammarDeque[cell.gid];
            // 判断是不是规约项目
            if (cell.index == gm.right.length() || gm.right == "@")
            {
                a.insert(gm.left);
            }
            // 判断是不是终结符
            else
            {
                if (isSmallAlpha(gm.right[cell.index]))
                {
                    rVT.insert(gm.right[cell.index]);
                }
            }
        }
        for (char c : a)
        {
            for (char v : rVT)
            {
                if (followSets[c].s.find(v) != followSets[c].s.end())
                {
                    return true;
                }

            }
        }
    }
    return false;
}

bool SLR1Fun2()
{
    // 检查规约-规约冲突
    for (const auto& state : dfaStateVector)
    {
        // 规约项目的左边集合
        set<char> a;
        // 不是规约状态不考虑
        if (!state.isEnd) continue;

        // 规约状态
        for (int cellid : state.cellV)
        {
            // 拿到这个cell
            const dfaCell& cell = dfaCellVector[cellid];
            // 获取文法
            const grammarUnit gm = grammarDeque[cell.gid];
            // 判断是不是规约项目
            if (cell.index == gm.right.length() || gm.right == "@")
            {
                a.insert(gm.left);
            }
        }

        for (char c1 : a)
        {
            for (char c2 : a)
            {
                if (c1 != c2)
                {
                    // 判断followSets[c1]和followSets[c2]是否有交集
                    set<char> followSetC1 = followSets[c1].s;
                    set<char> followSetC2 = followSets[c2].s;
                    set<char> intersection;

                    // 利用STL算法求交集
                    set_intersection(
                        followSetC1.begin(), followSetC1.end(),
                        followSetC2.begin(), followSetC2.end(),
                        inserter(intersection, intersection.begin())
                    );

                    // 如果交集非空，说明存在规约-规约冲突
                    if (!intersection.empty())
                    {
                        return true;
                    }
                }
            }
        }
    }

    
    return false;
}


// SLR1分析
int SLR1Analyse()
{
    // 开始符号添加follow集合
    followSets['^'].s.insert('$');

    bool flag1 = SLR1Fun1();
    bool flag2 = SLR1Fun2();
    if (flag1 && flag2)
    {
        return 3;
    }
    else if (flag1)
    {
        return 1;
    }
    else if (flag2)
    {
        return 2;
    }
    // 没有冲突，是SLR(1)文法
    return 0;
}

struct SLRUnit
{
    map<char, string> m;
};

vector<SLRUnit> SLRVector;

// SLR1分析表
int getSLR1Table()
{
    // SLR1分析错误就直接停止
    int r = SLR1Analyse();
    if (r != 0) return r;
    // 如果分析正确，通过LR0构造SLR1分析表（必须先调用getLR0）
    for (const dfaState& ds : dfaStateVector)
    {
        SLRUnit slrunit = SLRUnit();
        // 如果是归约，得做特殊处理
        if (ds.isEnd)
        {
            // 规约项目的非终结符
            char gl;
            string gr;
            // 规约状态
            for (int cellid : ds.cellV)
            {
                // 拿到这个cell
                const dfaCell& cell = dfaCellVector[cellid];
                // 获取文法
                const grammarUnit gm = grammarDeque[cell.gid];
                // 判断是不是规约项目
                if (cell.index == gm.right.length() || gm.right == "@")
                {
                    gl = gm.left;
                    gr = gm.right;
                    break;  // 前面的SLR1校验保证了只有一个归约项目
                }
            }
            // 得到这个非终结符Follow集合
            set<char> follow = followSets[gl].s;
            // follow集合每个元素都能归约
            for (char ch : follow)
            {
                if (gl == trueStartSymbol) slrunit.m[ch] = "ACCEPT";
                else slrunit.m[ch] = "r(" + string(1, gl) + "->" + gr + ")";
            }
            // 对于下一个节点（可能会存在）
            for (const auto& next : ds.nextStateVector)
            {
                char ch = next.c;
                int sid = next.sid; //  下一个状态id

                // 获取下一个状态具体信息
                dfaState d = dfaStateVector[sid];
                if (isBigAlpha(ch))
                {
                    slrunit.m[ch] = to_string(sid);
                }
                else
                {
                    slrunit.m[ch] = "s" + to_string(sid);
                }
            }
        }
        else
        {
            for (const auto& next : ds.nextStateVector)
            {
                char ch = next.c;
                int sid = next.sid; //  下一个状态id
                // 获取下一个状态具体信息
                dfaState d = dfaStateVector[sid];
                if (isBigAlpha(ch))
                {
                    slrunit.m[ch] = to_string(sid);
                }
                else
                {
                    slrunit.m[ch] = "s" + to_string(sid);
                }
            }
        }
        SLRVector.push_back(slrunit);
    }
    return 0;
}


/*清空全局变量*/
void reset()
{
    grammarMap.clear();
    firstSets.clear();
    followSets.clear();
    LR0Result.clear();
    grammarDeque.clear();
    dfaStateVector.clear();
    dfaCellVector.clear();
    VT.clear();
    VN.clear();
    SLRVector.clear();
    scnt = 0;
    ccnt = 0;

}

/******************** UI界面 ***************************/
// 查看输入规则
void Widget::on_pushButton_7_clicked()
{
    QString message = "输入时可以只输入单一个大写字母作为非终结符号，非大写英文字母（除@外）作为终结符号，用@表示空串，默认左边出现的第一个大写字母为文法的开始符号\n同时，文法中含有或(|)，请分开两条输入";

    QMessageBox::information(this, "输入规则", message);
}

// 打开文法规则
void Widget::on_pushButton_3_clicked()
{
    QString filePath = QFileDialog::getOpenFileName(this, tr("选择文件"), QDir::homePath(), tr("文本文件 (*.txt);;所有文件 (*.*)"));

    if (!filePath.isEmpty())
    {
        ifstream inputFile;
        QTextCodec* code = QTextCodec::codecForName("GB2312");

        string selectedFile = code->fromUnicode(filePath.toStdString().c_str()).data();
        inputFile.open(selectedFile.c_str(), ios::in);


        //        cout<<filePath.toStdString();
        //        ifstream inputFile(filePath.toStdString());
        if (!inputFile) {
            QMessageBox::critical(this, "错误信息", "导入错误！无法打开文件，请检查路径和文件是否被占用！");
            cerr << "Error opening file." << endl;
        }
        // 读取文件内容并显示在 plainTextEdit_2
        stringstream buffer;
        buffer << inputFile.rdbuf();
        QString fileContents = QString::fromStdString(buffer.str());
        ui->plainTextEdit_2->setPlainText(fileContents);
    }
}

// 保存文法规则
void Widget::on_pushButton_4_clicked()
{
    QString saveFilePath = QFileDialog::getSaveFileName(this, tr("保存文法文件"), QDir::homePath(), tr("文本文件 (*.txt)"));
    if (!saveFilePath.isEmpty() && !ui->plainTextEdit_2->toPlainText().isEmpty()) {
        QFile outputFile(saveFilePath);
        if (outputFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream stream(&outputFile);
            stream << ui->plainTextEdit_2->toPlainText();
            outputFile.close();
            QMessageBox::about(this, "提示", "导出成功！");
        }
    }
    else if (ui->plainTextEdit_2->toPlainText().isEmpty())
    {
        QMessageBox::warning(this, tr("提示"), tr("输入框为空，请重试！"));
    }
}

// 求解first集合按钮
void Widget::on_pushButton_5_clicked()
{
    reset();
    QString grammar_q = ui->plainTextEdit_2->toPlainText();
    grammarStr = grammar_q.toStdString();
    handleGrammar();
    getFirstSets();

    QTableWidget* tableWidget = ui->tableWidget_3;

    // 清空表格内容
    tableWidget->clearContents();

    // 设置表格的列数
    tableWidget->setColumnCount(2);

    // 设置表头
    QStringList headerLabels;
    headerLabels << "非终结符" << "First集合";
    tableWidget->setHorizontalHeaderLabels(headerLabels);

    // 设置行数
    tableWidget->setRowCount(firstSets.size());

    // 遍历非终结符的First集合，将其展示在表格中
    int row = 0;
    for (const auto& entry : firstSets)
    {
        char nonTerminal = entry.first;
        const set<char>& firstSet = entry.second.s;

        // 在表格中设置非终结符
        QTableWidgetItem* nonTerminalItem = new QTableWidgetItem(QString(nonTerminal));
        tableWidget->setItem(row, 0, nonTerminalItem);

        // 在表格中设置First集合，将set<char>转换为逗号分隔的字符串
        QString firstSetString;
        for (char symbol : firstSet)
        {
            firstSetString += QString(symbol) + ",";
        }
        if (entry.second.isEpsilon)
        {
            firstSetString += QString('@') + ",";
        }
        // 去掉最后一个逗号
        if (!firstSetString.isEmpty())
        {
            firstSetString.chop(1);
        }

        QTableWidgetItem* firstSetItem = new QTableWidgetItem(firstSetString);
        tableWidget->setItem(row, 1, firstSetItem);

        // 增加行数
        ++row;
    }
}

// 求解follow集合按钮
void Widget::on_pushButton_6_clicked()
{
    reset();
    QString grammar_q = ui->plainTextEdit_2->toPlainText();
    grammarStr = grammar_q.toStdString();
    handleGrammar();
    getFirstSets();
    getFollowSets();


    // 清空TableWidget
    ui->tableWidget_4->clear();

    // 设置表格的行数和列数
    int rowCount = followSets.size();
    int columnCount = 2; // 两列
    ui->tableWidget_4->setRowCount(rowCount);
    ui->tableWidget_4->setColumnCount(columnCount);

    // 设置表头
    QStringList headers;
    headers << "非终结符" << "Follow集合";
    ui->tableWidget_4->setHorizontalHeaderLabels(headers);

    // 遍历followSets，将数据填充到TableWidget中
    int row = 0;
    for (const auto& entry : followSets) {
        // 获取非终结符和对应的followUnit
        char nonTerminal = entry.first;
        const followUnit& followSet = entry.second;

        // 在第一列设置非终结符
        QTableWidgetItem* nonTerminalItem = new QTableWidgetItem(QString(nonTerminal));
        ui->tableWidget_4->setItem(row, 0, nonTerminalItem);

        // 在第二列设置followUnit，使用逗号拼接
        QString followSetStr = "";
        for (char c : followSet.s) {
            followSetStr += c;
            followSetStr += ",";
        }
        followSetStr.chop(1); // 移除最后一个逗号
        QTableWidgetItem* followSetItem = new QTableWidgetItem(followSetStr);
        ui->tableWidget_4->setItem(row, 1, followSetItem);

        // 移动到下一行
        ++row;
    }
}

// 生成LR(0)DFA图
void Widget::on_pushButton_clicked()
{
    reset();
    ui->tableWidget->clear();
    QString grammar_q = ui->plainTextEdit_2->toPlainText();
    grammarStr = grammar_q.toStdString();
    handleGrammar();
    ui->plainTextEdit_4->setPlainText(LR0Result);
    getLR0();

    int numRows = dfaStateVector.size();
    int numCols = 2 + VT.size() + VN.size();

    ui->tableWidget->setRowCount(numRows);
    ui->tableWidget->setColumnCount(numCols);

    // Set the table headers
    QStringList headers;
    headers << "状态" << "状态内文法";
    map<char, int> c2int;
    int cnt = 0;
    for (char vt : VT) {
        headers << QString(vt);
        c2int[vt] = cnt++;
    }
    for (char vn : VN) {
        headers << QString(vn);
        c2int[vn] = cnt++;
    }
    ui->tableWidget->setHorizontalHeaderLabels(headers);

    // Populate the table with data
    for (int i = 0; i < numRows; ++i)
    {
        ui->tableWidget->setItem(i, 0, new QTableWidgetItem(QString::number(dfaStateVector[i].sid)));
        ui->tableWidget->setItem(i, 1, new QTableWidgetItem(QString::fromStdString(getStateGrammar(dfaStateVector[i]))));

        // Display nextStateVector
        for (int j = 0; j < dfaStateVector[i].nextStateVector.size(); ++j)
        {
            ui->tableWidget->setItem(i, 2 + c2int[dfaStateVector[i].nextStateVector[j].c], new QTableWidgetItem(QString::number(dfaStateVector[i].nextStateVector[j].sid)));
        }
    }
}

// 分析SLR(1)文法
void Widget::on_pushButton_2_clicked()
{
    reset();
    QString grammar_q = ui->plainTextEdit_2->toPlainText();
    grammarStr = grammar_q.toStdString();
    handleGrammar();
    getFirstSets();
    getFollowSets();
    getLR0();
    int result = getSLR1Table();
    switch (result)
    {
    case 1:
        ui->plainTextEdit->setPlainText("出现归约-移进冲突");
        break;
    case 2:
        ui->plainTextEdit->setPlainText("出现归约-归约冲突");
        break;
    case 3:
        ui->plainTextEdit->setPlainText("出现归约-移进冲突和归约-归约冲突");
        break;
    case 0:
    {
        ui->plainTextEdit->setPlainText("符合SLR(1)文法，请查看SLR(1)分析表！");
        ui->tableWidget_2->clear();
        VT.insert('$');
        int numRows = SLRVector.size();
        int numCols = 1 + VT.size() + VN.size();

        ui->tableWidget_2->setRowCount(numRows);
        ui->tableWidget_2->setColumnCount(numCols);
        // Set the table headers
        QStringList headers;
        headers << "状态";
        map<char, int> c2int;
        int cnt = 0;
        for (char vt : VT) {
            headers << QString(vt);
            c2int[vt] = cnt++;
        }
        for (char vn : VN) {
            headers << QString(vn);
            c2int[vn] = cnt++;
        }
        ui->tableWidget_2->setHorizontalHeaderLabels(headers);

        // Populate the table with data
        for (int i = 0; i < numRows; ++i)
        {
            ui->tableWidget_2->setItem(i, 0, new QTableWidgetItem(QString::number(i)));

            // Display nextStateVector
            for ( const auto& slrunit : SLRVector[i].m)
            {
                ui->tableWidget_2->setItem(i, 1 + c2int[slrunit.first], new QTableWidgetItem(QString::fromStdString(slrunit.second)));
            }
        }

        break;
    }

    }
}

