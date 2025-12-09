/*
 * 学号：2025214277 姓名：刘汉叶
 * 面向对象大作业 - 第一阶段完整实现 (含围棋自动数子判胜负) 
 * 包含：五子棋/围棋核心逻辑、MVC架构、存档/读档、悔棋、提子判断、BFS数子算法
 */

#include <iostream>
#include <vector>
#include <string>
#include <stack>
#include <fstream>
#include <sstream>
#include <memory>
#include <set>
#include <algorithm>
#include <iomanip>
#include <queue> // 新增：用于BFS算法

using namespace std;

// ==========================================
// 1. 基础数据结构与枚举
// ==========================================

enum PieceType { EMPTY = 0, BLACK = 1, WHITE = 2 };
enum GameType { GOMOKU = 1, GO = 2 };
enum GameStatus { PLAYING, BLACK_WIN, WHITE_WIN, DRAW };

// 用于表示坐标
struct Point {
    int x, y;
    bool operator<(const Point& other) const {
        if (x != other.x) return x < other.x;
        return y < other.y;
    }
    bool operator==(const Point& other) const {
        return x == other.x && y == other.y;
    }
};

// ==========================================
// 2. Model 层：棋盘与核心逻辑
// ==========================================

// 棋盘类：负责存储棋子数据
class Board {
private:
    int size;
    vector<vector<PieceType>> grid;

public:
    Board(int s) : size(s) {
        grid.resize(size, vector<PieceType>(size, EMPTY));
    }

    // 拷贝构造函数（用于备忘录模式保存状态）
    Board(const Board& other) : size(other.size), grid(other.grid) {}

    int getSize() const { return size; }
    
    bool isValidBounds(int x, int y) const {
        return x >= 0 && x < size && y >= 0 && y < size;
    }

    PieceType getPiece(int x, int y) const {
        if (!isValidBounds(x, y)) return EMPTY;
        return grid[x][y];
    }

    void setPiece(int x, int y, PieceType p) {
        if (isValidBounds(x, y)) grid[x][y] = p;
    }

    void clear() {
        for (auto& row : grid) fill(row.begin(), row.end(), EMPTY);
    }

    // 序列化用于存档
    string serialize() const {
        stringstream ss;
        ss << size << " ";
        for (int i = 0; i < size; ++i)
            for (int j = 0; j < size; ++j)
                ss << grid[i][j] << " ";
        return ss.str();
    }

    // 反序列化用于读档
    void deserialize(stringstream& ss) {
        ss >> size;
        grid.resize(size, vector<PieceType>(size));
        int temp;
        for (int i = 0; i < size; ++i) {
            for (int j = 0; j < size; ++j) {
                ss >> temp;
                grid[i][j] = static_cast<PieceType>(temp);
            }
        }
    }
};

// 抽象策略基类 (Strategy Pattern)：定义游戏规则接口
class GameRule {
protected:
    Board* board;
public:
    GameRule(Board* b) : board(b) {}
    virtual ~GameRule() {}

    // 判断落子是否合法
    virtual bool isValidMove(int x, int y, PieceType player) = 0;
    
    // 执行落子（包括提子等副作用）
    virtual void makeMove(int x, int y, PieceType player) = 0;
    
    // 判断胜负
    virtual GameStatus checkWin(int lastX, int lastY) = 0;
    
    // 围棋特有的“虚着”处理
    virtual bool supportsPass() const { return false; }
};

// 五子棋规则实现
class GomokuRule : public GameRule {
public:
    GomokuRule(Board* b) : GameRule(b) {}

    bool isValidMove(int x, int y, PieceType player) override {
        return board->isValidBounds(x, y) && board->getPiece(x, y) == EMPTY;
    }

    void makeMove(int x, int y, PieceType player) override {
        board->setPiece(x, y, player);
    }

    GameStatus checkWin(int x, int y) override {
        if (x == -1 && y == -1) return PLAYING; // 虚着或初始状态
        
        PieceType current = board->getPiece(x, y);
        if (current == EMPTY) return PLAYING;

        int directions[4][2] = {{1, 0}, {0, 1}, {1, 1}, {1, -1}}; // 横、竖、正斜、反斜

        for (auto& dir : directions) {
            int count = 1;
            // 正向查找
            for (int i = 1; i < 5; ++i) {
                if (board->getPiece(x + i * dir[0], y + i * dir[1]) == current) count++;
                else break;
            }
            // 反向查找
            for (int i = 1; i < 5; ++i) {
                if (board->getPiece(x - i * dir[0], y - i * dir[1]) == current) count++;
                else break;
            }
            if (count >= 5) return (current == BLACK) ? BLACK_WIN : WHITE_WIN;
        }

        // 检查平局（棋盘满）
        bool full = true;
        for(int i=0; i<board->getSize(); ++i)
            for(int j=0; j<board->getSize(); ++j)
                if(board->getPiece(i, j) == EMPTY) full = false;
        
        return full ? DRAW : PLAYING;
    }
};

// 围棋规则实现 (含提子、气判断、数子法胜负计算)
class GoRule : public GameRule {
private:
    bool visited[19][19]; // 辅助DFS

    // 获取某个棋子所在群组的气
    int getLiberties(int x, int y, PieceType color, vector<Point>& group) {
        // 重置访问标记 (局部优化，实际工程中应优化内存)
        int size = board->getSize();
        for(int i=0; i<size; ++i)
            for(int j=0; j<size; ++j) visited[i][j] = false;

        return countLibertiesDFS(x, y, color, group);
    }

    int countLibertiesDFS(int x, int y, PieceType color, vector<Point>& group) {
        if (!board->isValidBounds(x, y)) return 0;
        if (visited[x][y]) return 0;
        
        visited[x][y] = true;
        PieceType p = board->getPiece(x, y);

        if (p == EMPTY) return 1; // 找到气
        if (p != color) return 0; // 遇到对方棋子

        group.push_back({x, y}); // 加入同色群组
        
        int liberties = 0;
        int dx[] = {0, 0, 1, -1};
        int dy[] = {1, -1, 0, 0};

        for (int i = 0; i < 4; ++i) {
            liberties += countLibertiesDFS(x + dx[i], y + dy[i], color, group);
        }
        return liberties;
    }

    // 移除死子
    void removeDeadStones(int x, int y, PieceType opponent) {
        int dx[] = {0, 0, 1, -1};
        int dy[] = {1, -1, 0, 0};

        for (int i = 0; i < 4; ++i) {
            int nx = x + dx[i];
            int ny = y + dy[i];
            if (board->isValidBounds(nx, ny) && board->getPiece(nx, ny) == opponent) {
                vector<Point> group;
                if (getLiberties(nx, ny, opponent, group) == 0) {
                    // 气为0，提子
                    for (auto& p : group) {
                        board->setPiece(p.x, p.y, EMPTY);
                    }
                }
            }
        }
    }

public:
    GoRule(Board* b) : GameRule(b) {}

    // === 新增：围棋胜负结果结构体 ===
    struct GoResult {
        float blackScore;
        float whiteScore;
        string winner;
    };

    // === 新增：使用中国规则（数子法）计算胜负 ===
    // 算法逻辑：遍历棋盘，如果是棋子则计分；如果是空地，BFS搜索区域，判断区域被谁包围。
    GoResult calculateFinalScore() {
        int size = board->getSize();
        float blackCount = 0;
        float whiteCount = 0;
        // 标记数组，用于防止BFS重复访问
        vector<vector<bool>> checked(size, vector<bool>(size, false));

        int dx[] = {0, 0, 1, -1};
        int dy[] = {1, -1, 0, 0};

        for(int i=0; i<size; ++i) {
            for(int j=0; j<size; ++j) {
                if(checked[i][j]) continue; // 已经统计过，跳过

                PieceType p = board->getPiece(i, j);
                if(p == BLACK) {
                    blackCount += 1.0;
                    checked[i][j] = true;
                } else if(p == WHITE) {
                    whiteCount += 1.0;
                    checked[i][j] = true;
                } else {
                    // 发现未访问的空地，启动 BFS 寻找连通区域并判断归属
                    vector<Point> territory;
                    queue<Point> q;
                    
                    q.push({i, j});
                    checked[i][j] = true;
                    territory.push_back({i, j});

                    bool touchesBlack = false;
                    bool touchesWhite = false;

                    while(!q.empty()) {
                        Point cur = q.front();
                        q.pop();

                        for(int k=0; k<4; ++k) {
                            int nx = cur.x + dx[k];
                            int ny = cur.y + dy[k];

                            if(board->isValidBounds(nx, ny)) {
                                PieceType neighbor = board->getPiece(nx, ny);
                                if(neighbor == EMPTY) {
                                    if(!checked[nx][ny]) {
                                        checked[nx][ny] = true;
                                        q.push({nx, ny});
                                        territory.push_back({nx, ny});
                                    }
                                } else if(neighbor == BLACK) {
                                    touchesBlack = true;
                                } else if(neighbor == WHITE) {
                                    touchesWhite = true;
                                }
                            }
                        }
                    }

                    // 判定领地归属
                    if(touchesBlack && !touchesWhite) {
                        blackCount += territory.size(); // 纯黑地，计入黑方
                    } else if(!touchesBlack && touchesWhite) {
                        whiteCount += territory.size(); // 纯白地，计入白方
                    } 
                    // 如果都接触(公气)或都不接触(死棋未提)，则不计分，符合中国规则
                }
            }
        }

        // 中国规则：黑棋先行贴3.75子 (相当于日韩规则的7.5目)
        whiteCount += 3.75f;

        GoResult res;
        res.blackScore = blackCount;
        res.whiteScore = whiteCount;
        if(blackCount > whiteCount) res.winner = "黑方 (Black)";
        else res.winner = "白方 (White)";

        return res;
    }

    bool supportsPass() const override { return true; }

    bool isValidMove(int x, int y, PieceType player) override {
        if (!board->isValidBounds(x, y)) return false;
        if (board->getPiece(x, y) != EMPTY) return false;

        // 临时落子测试
        board->setPiece(x, y, player);
        
        // 1. 检查是否能提掉对方
        bool captures = false;
        PieceType opponent = (player == BLACK) ? WHITE : BLACK;
        int dx[] = {0, 0, 1, -1};
        int dy[] = {1, -1, 0, 0};
        
        for (int i = 0; i < 4; ++i) {
            int nx = x + dx[i];
            int ny = y + dy[i];
            if (board->isValidBounds(nx, ny) && board->getPiece(nx, ny) == opponent) {
                vector<Point> group;
                if (getLiberties(nx, ny, opponent, group) == 0) {
                    captures = true;
                }
            }
        }

        // 2. 如果没提子，检查自己是否有气 (禁入点判断)
        bool suicide = false;
        if (!captures) {
            vector<Point> selfGroup;
            if (getLiberties(x, y, player, selfGroup) == 0) {
                suicide = true;
            }
        }

        // 撤销临时落子
        board->setPiece(x, y, EMPTY);

        return !suicide;
    }

    void makeMove(int x, int y, PieceType player) override {
        if (x == -1 && y == -1) return; // 虚着

        board->setPiece(x, y, player);
        removeDeadStones(x, y, (player == BLACK) ? WHITE : BLACK);
    }

    GameStatus checkWin(int lastX, int lastY) override {
        // 围棋胜负由 makeMove 后的双人虚着触发，此处保持返回 PLAYING
        return PLAYING;
    }
};

// ==========================================
// 3. View 层：界面显示 (Observer/View Interface)
// ==========================================

class GameView {
public:
    virtual void displayBoard(const Board& board, PieceType currentPlayer, string msg = "") = 0;
    virtual string getUserInput(string prompt) = 0;
    virtual void showHelp() = 0;
    virtual ~GameView() {}
};

// 控制台视图实现
class ConsoleView : public GameView {
public:
    void displayBoard(const Board& board, PieceType currentPlayer, string msg) override {
        // 清屏
        #ifdef _WIN32
            system("cls");
        #else
            system("clear");
        #endif

        int size = board.getSize();
        cout << "   ";
        for (int i = 0; i < size; ++i) cout << setw(2) << i + 1 << " ";
        cout << endl;

        for (int i = 0; i < size; ++i) {
            cout << setw(2) << i + 1 << " ";
            for (int j = 0; j < size; ++j) {
                PieceType p = board.getPiece(i, j);
                if (p == BLACK) cout << " X "; // 黑子
                else if (p == WHITE) cout << " O "; // 白子
                else cout << " + ";
            }
            cout << endl;
        }
        cout << "-----------------------------------" << endl;
        cout << "当前执子: " << (currentPlayer == BLACK ? "黑方 (X)" : "白方 (O)") << endl;
        if (!msg.empty()) cout << "提示: " << msg << endl; 
    }

    string getUserInput(string prompt) override {
        cout << prompt;
        string input;
        getline(cin, input);
        return input;
    }

    void showHelp() override {
        cout << "指令说明:" << endl;
        cout << "  x y  : 落子 (例如: 3 4)" << endl;
        cout << "  pass : 虚着 (仅围棋)" << endl;
        cout << "  undo : 悔棋 " << endl;
        cout << "  save : 存档 " << endl;
        cout << "  load : 读档 " << endl;
        cout << "  quit : 认输/退出 " << endl;
        cout << "按回车键继续...";
        string dummy; getline(cin, dummy);
    }
};

// ==========================================
// 4. Controller 层：游戏管理器
// ==========================================

struct GameState {
    Board board;
    PieceType currentPlayer;
    int passCount; // 连续虚着次数
    
    GameState(int size) : board(size), currentPlayer(BLACK), passCount(0) {}
};

class GameManager {
private:
    unique_ptr<Board> board;
    unique_ptr<GameRule> rule;
    unique_ptr<GameView> view;
    
    PieceType currentPlayer;
    GameType gameType;
    int passCount; // 用于判断围棋终局 (连续两次虚着)
    
    stack<GameState> history; // 备忘录模式：历史记录用于悔棋
    bool showHints;

    void saveState() {
        GameState state(board->getSize());
        state.board = *board; // copy
        state.currentPlayer = currentPlayer;
        state.passCount = passCount;
        history.push(state);
    }

public:
    GameManager() : currentPlayer(BLACK), passCount(0), showHints(true) {
        view = unique_ptr<GameView>(new ConsoleView()); // 默认使用控制台视图
    }

    void initGame() {
        while (true) {
            string typeStr = view->getUserInput("请选择游戏 (1:五子棋, 2:围棋): ");
            if (typeStr == "1" || typeStr == "2") {
                gameType = (typeStr == "1") ? GOMOKU : GO;
                break;
            }
        }

        int size = 0;
        while (true) {
            string sizeStr = view->getUserInput("请输入棋盘大小 (8-19): ");
            try {
                size = stoi(sizeStr);
                if (size >= 8 && size <= 19) break;
            } catch(...) {}
            cout << "输入无效，请重新输入。" << endl;
        }

        board = unique_ptr<Board>(new Board(size));
        
        // 工厂模式体现：根据类型创建规则
        if (gameType == GOMOKU)
            rule = unique_ptr<GameRule>(new GomokuRule(board.get()));
        else
            rule = unique_ptr<GameRule>(new GoRule(board.get()));
        
        currentPlayer = BLACK;
        passCount = 0;
        while(!history.empty()) history.pop(); // 清空历史
    }

    void saveGame(string filename) { 
        ofstream outfile(filename);
        if (!outfile.is_open()) {
            view->displayBoard(*board, currentPlayer, "保存文件失败!");
            return;
        }
        outfile << (int)gameType << " " << (int)currentPlayer << " " << passCount << endl;
        outfile << board->serialize();
        outfile.close();
        view->displayBoard(*board, currentPlayer, "游戏已保存至 " + filename);
    }

    bool loadGame(string filename) { 
        ifstream infile(filename);
        if (!infile.is_open()) {
            return false;
        }
        int type, player;
        infile >> type >> player >> passCount;
        gameType = (GameType)type;
        currentPlayer = (PieceType)player;
        
        stringstream ss;
        ss << infile.rdbuf(); // 读取剩余内容
        
        // 重建对象
        board = unique_ptr<Board>(new Board(0)); // 临时
        board->deserialize(ss);
        
        if (gameType == GOMOKU)
            rule = unique_ptr<GameRule>(new GomokuRule(board.get()));
        else
            rule = unique_ptr<GameRule>(new GoRule(board.get()));
            
        return true;
    }

    void run() {
        initGame();
        string message = "游戏开始！输入 'help' 查看指令。";

        bool running = true;
        while (running) {
            view->displayBoard(*board, currentPlayer, message);
            message = "";

            string input = view->getUserInput("请输入指令 > ");
            
            // 指令解析
            if (input == "quit") { 
                if(view->getUserInput("确认认输/退出吗? (y/n): ") == "y") break;
                continue;
            }
            else if (input == "help") {
                view->showHelp();
                continue;
            }
            else if (input == "undo") { 
                if (history.empty()) {
                    message = "无法悔棋，没有历史记录";
                } else {
                    GameState prev = history.top();
                    history.pop();
                    *board = prev.board;
                    currentPlayer = prev.currentPlayer;
                    passCount = prev.passCount;
                    message = "已悔棋一步";
                }
                continue;
            }
            else if (input.substr(0, 4) == "save") {
                string fname = (input.length() > 5) ? input.substr(5) : "savegame.txt";
                saveGame(fname);
                continue;
            }
            else if (input.substr(0, 4) == "load") {
                string fname = (input.length() > 5) ? input.substr(5) : "savegame.txt";
                if(loadGame(fname)) message = "读取成功 ";
                else message = "读取失败或文件不存在";
                continue;
            }

            // 处理落子逻辑
            int x = -1, y = -1;
            bool isPass = false;

            if (input == "pass") { 
                if (gameType == GO) isPass = true;
                else {
                    message = "五子棋不能虚着";
                    continue;
                }
            } else {
                stringstream ss(input);
                if (!(ss >> x >> y)) {
                    message = "指令无效";
                    continue;
                }
                x--; y--; // 转换为0索引
            }

            // 逻辑判断
            if (isPass) {
                saveState(); // 记录状态以便悔棋
                passCount++;
                if (passCount >= 2) { 
                    // === 修改部分：围棋双人虚着终局判断 ===
                    if (gameType == GO) {
                        // 使用 dynamic_cast 安全转换基类指针到派生类指针，以便调用 calculateFinalScore
                        GoRule* goRule = dynamic_cast<GoRule*>(rule.get());
                        if (goRule) {
                            view->displayBoard(*board, currentPlayer, "正在计算胜负...");
                            auto res = goRule->calculateFinalScore();
                            
                            cout << "\n==================================" << endl;
                            cout << "         游戏结束 (双人虚着)        " << endl;
                            cout << "==================================" << endl;
                            cout << fixed << setprecision(2);
                            cout << "黑方 (子+地): " << res.blackScore << endl;
                            cout << "白方 (子+地+贴目3.75): " << res.whiteScore << endl;
                            cout << "----------------------------------" << endl;
                            cout << "最终胜者: " << res.winner << endl;
                            cout << "==================================" << endl;
                        }
                    } else {
                        view->displayBoard(*board, currentPlayer, "双方虚着，游戏结束！");
                    }
                    running = false;
                } else {
                    currentPlayer = (currentPlayer == BLACK) ? WHITE : BLACK;
                    message = "玩家虚着 (再Pass一次即终局数子)";
                }
            } else {
                if (rule->isValidMove(x, y, currentPlayer)) {
                    saveState();
                    rule->makeMove(x, y, currentPlayer);
                    passCount = 0; // 落子则重置虚着计数
                    
                    // 胜负判断
                    GameStatus status = rule->checkWin(x, y);
                    if (status != PLAYING) {
                        view->displayBoard(*board, currentPlayer, "");
                        cout << "\n================================" << endl;
                        if (status == BLACK_WIN) cout << "   黑方获胜！" << endl;
                        else if (status == WHITE_WIN) cout << "   白方获胜！" << endl;
                        else cout << "   平局！" << endl;
                        cout << "================================" << endl;
                        running = false;
                    } else {
                        currentPlayer = (currentPlayer == BLACK) ? WHITE : BLACK;
                    }
                } else {
                    message = "落子不合法 (位置占用或违规)";
                }
            }
        }
    }
};

int main() {
    // 程序入口
    GameManager game;
    game.run();
    return 0;
}