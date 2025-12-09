/*
 * 面向对象大作业 - 第四阶段：AI 难度选择增强版
 * 包含：五子棋/围棋/黑白棋、账户管理、录像回放、MVC架构
 * 核心功能：
 * 1. MCTS (蒙特卡洛树搜索) AI (Level 3)
 * 2. 贪心算法 AI (Level 2)
 * 3. 随机算法 AI (Level 1)
 * 4. 支持 人机对战 和 机机对战 的双向难度自由选择
 */

#include <iostream>
#include <vector>
#include <string>
#include <stack>
#include <fstream>
#include <sstream>
#include <memory>
#include <algorithm>
#include <iomanip>
#include <queue>
#include <map>
#include <ctime>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <cmath>
#include <limits>

using namespace std;

// ==========================================
// 1. 基础数据结构与枚举
// ==========================================

enum PieceType { EMPTY = 0, BLACK = 1, WHITE = 2 };
enum GameType { GOMOKU = 1, GO = 2, REVERSI = 3 };
enum GameStatus { PLAYING, BLACK_WIN, WHITE_WIN, DRAW };
enum PlayerType { HUMAN = 0, AI_LEVEL_1 = 1, AI_LEVEL_2 = 2, AI_LEVEL_3 = 3 };

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

// 获取对手颜色
PieceType getOpponent(PieceType p) {
    return (p == BLACK) ? WHITE : BLACK;
}

// ==========================================
// 2. 账户管理系统 (Account Management)
// ==========================================

struct User {
    string username;
    string password;
    int wins;
    int totalGames;
};

class UserManager {
private:
    string filename = "users.txt";
    map<string, User> users;
    string currentUser;

public:
    UserManager() { loadUsers(); }

    void loadUsers() {
        ifstream file(filename);
        if (!file.is_open()) return;
        string u, p;
        int w, t;
        while (file >> u >> p >> w >> t) {
            users[u] = {u, p, w, t};
        }
        file.close();
    }

    void saveUsers() {
        ofstream file(filename);
        for (auto const& pair : users) {
            const User& user = pair.second;
            file << user.username << " " << user.password << " " << user.wins << " " << user.totalGames << endl;
        }
        file.close();
    }

    bool registerUser(string username, string password) {
        if (users.find(username) != users.end()) return false;
        users[username] = {username, password, 0, 0};
        saveUsers();
        return true;
    }

    bool login(string username, string password) {
        if (users.find(username) == users.end()) return false;
        if (users[username].password == password) {
            currentUser = username;
            return true;
        }
        return false;
    }

    void logout() { currentUser = ""; }
    bool isLoggedIn() const { return !currentUser.empty(); }
    string getCurrentUsername() const { return currentUser.empty() ? "Guest" : currentUser; }

    void recordGameResult(bool isWin) {
        if (currentUser.empty()) return;
        users[currentUser].totalGames++;
        if (isWin) users[currentUser].wins++;
        saveUsers();
    }

    string getStats(string username) {
        if (users.find(username) == users.end()) return "Guest (No Record)";
        User& u = users[username];
        stringstream ss;
        ss << u.username << " [Wins: " << u.wins << "/" << u.totalGames << "]";
        return ss.str();
    }
};

// ==========================================
// 3. Model 层：棋盘与规则
// ==========================================

class Board {
private:
    int size;
    vector<vector<PieceType>> grid;

public:
    Board(int s) : size(s) {
        grid.resize(size, vector<PieceType>(size, EMPTY));
    }
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

    int countPieces(PieceType type) const {
        int count = 0;
        for(auto& row : grid)
            for(auto p : row) if(p == type) count++;
        return count;
    }

    string serialize() const {
        stringstream ss;
        ss << size << " ";
        for (int i = 0; i < size; ++i)
            for (int j = 0; j < size; ++j)
                ss << grid[i][j] << " ";
        return ss.str();
    }

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

class GameRule {
protected:
    Board* board;
public:
    GameRule(Board* b) : board(b) {}
    virtual ~GameRule() {}
    
    // MCTS 关键：原型模式克隆接口
    virtual GameRule* clone(Board* newBoard) const = 0;

    virtual bool isValidMove(int x, int y, PieceType player) = 0;
    virtual void makeMove(int x, int y, PieceType player) = 0;
    virtual GameStatus checkWin(int lastX, int lastY) = 0;
    virtual bool supportsPass() const { return false; } 
    virtual void initBoard() {} 
    
    virtual void calculateScore(float& blackScore, float& whiteScore) {
        blackScore = board->countPieces(BLACK);
        whiteScore = board->countPieces(WHITE);
    }
    
    bool hasValidMove(PieceType player) {
        for(int i=0; i<board->getSize(); ++i)
            for(int j=0; j<board->getSize(); ++j)
                if(isValidMove(i, j, player)) return true;
        return false;
    }
};

// --- 五子棋规则 ---
class GomokuRule : public GameRule {
public:
    GomokuRule(Board* b) : GameRule(b) {}
    
    GameRule* clone(Board* newBoard) const override {
        return new GomokuRule(newBoard);
    }

    bool isValidMove(int x, int y, PieceType player) override {
        return board->isValidBounds(x, y) && board->getPiece(x, y) == EMPTY;
    }
    void makeMove(int x, int y, PieceType player) override {
        board->setPiece(x, y, player);
    }
    GameStatus checkWin(int x, int y) override {
        if (x == -1 && y == -1) return PLAYING;
        PieceType current = board->getPiece(x, y);
        if (current == EMPTY) return PLAYING;
        int directions[4][2] = {{1, 0}, {0, 1}, {1, 1}, {1, -1}};
        for (auto& dir : directions) {
            int count = 1;
            for (int i = 1; i < 5; ++i) {
                if (board->getPiece(x + i * dir[0], y + i * dir[1]) == current) count++;
                else break;
            }
            for (int i = 1; i < 5; ++i) {
                if (board->getPiece(x - i * dir[0], y - i * dir[1]) == current) count++;
                else break;
            }
            if (count >= 5) return (current == BLACK) ? BLACK_WIN : WHITE_WIN;
        }
        for(int i=0; i<board->getSize(); ++i)
            for(int j=0; j<board->getSize(); ++j)
                if(board->getPiece(i, j) == EMPTY) return PLAYING;
        return DRAW;
    }
};

// --- 围棋规则 ---
class GoRule : public GameRule {
private:
    bool visited[19][19];
    int countLibertiesDFS(int x, int y, PieceType color, vector<Point>& group) {
        if (!board->isValidBounds(x, y)) return 0;
        if (visited[x][y]) return 0;
        visited[x][y] = true;
        PieceType p = board->getPiece(x, y);
        if (p == EMPTY) return 1;
        if (p != color) return 0;
        group.push_back({x, y});
        int liberties = 0;
        int dx[] = {0, 0, 1, -1}; int dy[] = {1, -1, 0, 0};
        for (int i = 0; i < 4; ++i) liberties += countLibertiesDFS(x + dx[i], y + dy[i], color, group);
        return liberties;
    }
    int getLiberties(int x, int y, PieceType color, vector<Point>& group) {
        int s = board->getSize();
        for(int i=0; i<s; ++i) for(int j=0; j<s; ++j) visited[i][j] = false;
        return countLibertiesDFS(x, y, color, group);
    }
    void removeDeadStones(int x, int y, PieceType opponent) {
        int dx[] = {0, 0, 1, -1}; int dy[] = {1, -1, 0, 0};
        for (int i = 0; i < 4; ++i) {
            int nx = x + dx[i], ny = y + dy[i];
            if (board->isValidBounds(nx, ny) && board->getPiece(nx, ny) == opponent) {
                vector<Point> group;
                if (getLiberties(nx, ny, opponent, group) == 0) {
                    for (auto& p : group) board->setPiece(p.x, p.y, EMPTY);
                }
            }
        }
    }
public:
    GoRule(Board* b) : GameRule(b) {}
    
    GameRule* clone(Board* newBoard) const override {
        return new GoRule(newBoard);
    }

    bool supportsPass() const override { return true; }
    bool isValidMove(int x, int y, PieceType player) override {
        if (!board->isValidBounds(x, y) || board->getPiece(x, y) != EMPTY) return false;
        board->setPiece(x, y, player); // 试下
        bool captures = false;
        PieceType opponent = getOpponent(player);
        int dx[] = {0, 0, 1, -1}; int dy[] = {1, -1, 0, 0};
        for (int i = 0; i < 4; ++i) {
            int nx = x + dx[i], ny = y + dy[i];
            if (board->isValidBounds(nx, ny) && board->getPiece(nx, ny) == opponent) {
                vector<Point> group;
                if (getLiberties(nx, ny, opponent, group) == 0) captures = true;
            }
        }
        bool suicide = false;
        if (!captures) {
            vector<Point> selfGroup;
            if (getLiberties(x, y, player, selfGroup) == 0) suicide = true;
        }
        board->setPiece(x, y, EMPTY); // 撤销
        return !suicide;
    }
    void makeMove(int x, int y, PieceType player) override {
        if (x == -1 && y == -1) return;
        board->setPiece(x, y, player);
        removeDeadStones(x, y, getOpponent(player));
    }
    GameStatus checkWin(int x, int y) override { return PLAYING; } 
    
    void calculateScore(float& blackScore, float& whiteScore) override {
        int size = board->getSize();
        blackScore = 0;
        whiteScore = 0;
        vector<vector<bool>> checked(size, vector<bool>(size, false));
        int dx[] = {0, 0, 1, -1};
        int dy[] = {1, -1, 0, 0};

        for(int i=0; i<size; ++i) {
            for(int j=0; j<size; ++j) {
                if(checked[i][j]) continue;

                PieceType p = board->getPiece(i, j);
                if(p == BLACK) {
                    blackScore += 1.0;
                    checked[i][j] = true;
                } else if(p == WHITE) {
                    whiteScore += 1.0;
                    checked[i][j] = true;
                } else {
                    vector<Point> territory;
                    queue<Point> q;
                    q.push({i, j});
                    checked[i][j] = true;
                    territory.push_back({i, j});
                    bool touchesBlack = false; bool touchesWhite = false;

                    while(!q.empty()) {
                        Point cur = q.front(); q.pop();
                        for(int k=0; k<4; ++k) {
                            int nx = cur.x + dx[k]; int ny = cur.y + dy[k];
                            if(board->isValidBounds(nx, ny)) {
                                PieceType neighbor = board->getPiece(nx, ny);
                                if(neighbor == EMPTY) {
                                    if(!checked[nx][ny]) {
                                        checked[nx][ny] = true;
                                        q.push({nx, ny});
                                        territory.push_back({nx, ny});
                                    }
                                } else if(neighbor == BLACK) touchesBlack = true;
                                else if(neighbor == WHITE) touchesWhite = true;
                            }
                        }
                    }
                    if(touchesBlack && !touchesWhite) blackScore += territory.size();
                    else if(!touchesBlack && touchesWhite) whiteScore += territory.size();
                }
            }
        }
        whiteScore += 3.75; 
    }
};

// --- 黑白棋 (Reversi) 规则 ---
class ReversiRule : public GameRule {
private:
    bool checkDirection(int x, int y, int dx, int dy, PieceType player, bool flip) {
        PieceType opponent = getOpponent(player);
        int i = 1;
        bool hasOpponent = false;
        while (true) {
            int nx = x + i * dx;
            int ny = y + i * dy;
            if (!board->isValidBounds(nx, ny)) return false;
            PieceType p = board->getPiece(nx, ny);
            if (p == EMPTY) return false;
            if (p == opponent) hasOpponent = true;
            else if (p == player) {
                if (hasOpponent) {
                    if (flip) {
                        for (int k = 1; k < i; ++k) board->setPiece(x + k * dx, y + k * dy, player);
                    }
                    return true;
                } else return false;
            }
            i++;
        }
    }

public:
    ReversiRule(Board* b) : GameRule(b) {}
    
    GameRule* clone(Board* newBoard) const override {
        return new ReversiRule(newBoard);
    }

    void initBoard() override {
        int mid = board->getSize() / 2;
        board->setPiece(mid - 1, mid - 1, WHITE);
        board->setPiece(mid, mid, WHITE);
        board->setPiece(mid - 1, mid, BLACK);
        board->setPiece(mid, mid - 1, BLACK);
    }

    bool supportsPass() const override { return true; } 

    bool isValidMove(int x, int y, PieceType player) override {
        if (!board->isValidBounds(x, y) || board->getPiece(x, y) != EMPTY) return false;
        int dx[] = {0, 0, 1, -1, 1, 1, -1, -1};
        int dy[] = {1, -1, 0, 0, 1, -1, 1, -1};
        for (int i = 0; i < 8; ++i) {
            if (checkDirection(x, y, dx[i], dy[i], player, false)) return true;
        }
        return false;
    }

    void makeMove(int x, int y, PieceType player) override {
        if (x == -1 && y == -1) return;
        board->setPiece(x, y, player);
        int dx[] = {0, 0, 1, -1, 1, 1, -1, -1};
        int dy[] = {1, -1, 0, 0, 1, -1, 1, -1};
        for (int i = 0; i < 8; ++i) {
            checkDirection(x, y, dx[i], dy[i], player, true); 
        }
    }

    GameStatus checkWin(int x, int y) override {
        if (board->countPieces(EMPTY) == 0) {
            int b = board->countPieces(BLACK);
            int w = board->countPieces(WHITE);
            if (b > w) return BLACK_WIN;
            if (w > b) return WHITE_WIN;
            return DRAW;
        }
        return PLAYING;
    }
};

// ==========================================
// 4. View 层
// ==========================================

class GameView {
public:
    virtual void displayBoard(const Board& board, PieceType currentPlayer, string msg = "") = 0;
    virtual string getUserInput(string prompt) = 0;
    virtual void showMainMenu() = 0;
    virtual ~GameView() {}
};

class ConsoleView : public GameView {
public:
    void displayBoard(const Board& board, PieceType currentPlayer, string msg) override {
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
                if (p == BLACK) cout << " X "; 
                else if (p == WHITE) cout << " O "; 
                else cout << " . ";
            }
            cout << endl;
        }
        cout << "-----------------------------------" << endl;
        if (currentPlayer != EMPTY)
            cout << "当前执子: " << (currentPlayer == BLACK ? "黑方 (X)" : "白方 (O)") << endl;
        if (!msg.empty()) cout << ">> " << msg << endl; 
    }

    string getUserInput(string prompt) override {
        cout << prompt;
        string input;
        getline(cin, input);
        return input;
    }

    void showMainMenu() override {
        cout << "\n=== 棋类对战平台 ===" << endl;
        cout << "1. 登录 (Login)" << endl;
        cout << "2. 注册 (Register)" << endl;
        cout << "3. 游客试玩 (Guest)" << endl;
        cout << "4. 退出 (Exit)" << endl;
        cout << "====================" << endl;
    }
};

// ==========================================
// 5. Player 抽象与 AI 实现 (Strategy Pattern)
// ==========================================

class Player {
protected:
    string name;
    PieceType color;
public:
    Player(string n, PieceType c) : name(n), color(c) {}
    virtual ~Player() {}
    virtual Point getMove(const Board& board, GameRule* rule, GameView* view) = 0;
    string getName() const { return name; }
    PieceType getColor() const { return color; }
    bool isAI() const { return name.find("AI") != string::npos; }
};

class HumanPlayer : public Player {
public:
    HumanPlayer(string n, PieceType c) : Player(n, c) {}
    Point getMove(const Board& board, GameRule* rule, GameView* view) override {
        while(true) {
            string input = view->getUserInput("请输入坐标 (x y) 或指令(undo/save/pass): ");
            if (input == "undo" || input == "save" || input == "quit" || input == "pass") {
                if (input == "undo") return {-2, -2};
                if (input == "save") return {-3, -3};
                if (input == "quit") return {-4, -4};
                if (input == "pass") return {-1, -1};
            }
            stringstream ss(input);
            int x, y;
            if (ss >> x >> y) {
                return {x - 1, y - 1};
            }
            view->displayBoard(board, color, "输入无效，请重新输入。");
        }
    }
};

// --- MCTS 节点结构 ---
struct MCTSNode {
    MCTSNode* parent;
    vector<MCTSNode*> children;
    Point move; // 到达此节点的落子
    PieceType playerMoved; // 谁落子到达了这里
    int visits;
    double wins; // 针对 playerMoved 的胜场价值累加
    vector<Point> untriedMoves;

    MCTSNode(MCTSNode* p, Point m, PieceType player, const Board& board, GameRule* rule) 
        : parent(p), move(m), playerMoved(player), visits(0), wins(0.0) 
    {
        // 找出所有未尝试的移动
        PieceType nextP = getOpponent(player);
        for(int i=0; i<board.getSize(); ++i) {
            for(int j=0; j<board.getSize(); ++j) {
                if(rule->isValidMove(i, j, nextP)) {
                    untriedMoves.push_back({i, j});
                }
            }
        }
    }

    ~MCTSNode() {
        for(auto c : children) delete c;
    }
    
    // UCT 选择公式 (Upper Confidence Bound for Trees)
    MCTSNode* bestChild(double cParam = 1.414) {
        MCTSNode* best = nullptr;
        double bestValue = -std::numeric_limits<double>::infinity();
        for(auto child : children) {
            double uct = (child->wins / (double)child->visits) + 
                         cParam * sqrt(log(visits) / (double)child->visits);
            if(uct > bestValue) {
                bestValue = uct;
                best = child;
            }
        }
        return best;
    }
};

class AIPlayer : public Player {
private:
    int level; 
public:
    AIPlayer(string n, PieceType c, int lvl) : Player(n, c), level(lvl) {}
    
    // Level 3: MCTS AI 实现
    Point getMCTSMove(const Board& realBoard, GameRule* realRule) {
        // 1. 复制当前局面，避免破坏真实棋盘
        Board rootBoard(realBoard);
        unique_ptr<GameRule> rootRule(realRule->clone(&rootBoard));
        
        // 根节点：上一手是对手下的，现在轮到我 (color) 下
        MCTSNode* root = new MCTSNode(nullptr, {-1,-1}, getOpponent(color), rootBoard, rootRule.get());
        
        // 设定思考时间限制 (例如 2 秒)
        auto startTime = std::chrono::high_resolution_clock::now();
        int iterations = 0;
        
        while(true) {
            auto now = std::chrono::high_resolution_clock::now();
            if(std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count() > 2000) break;
            
            iterations++;
            
            // --- 1. Selection (选择) ---
            MCTSNode* node = root;
            Board simBoard(rootBoard);
            unique_ptr<GameRule> simRule(rootRule->clone(&simBoard));
            PieceType simPlayer = color; // 从当前 AI 开始模拟
            
            // 只要节点完全扩展且有子节点，就根据 UCT 向下深入
            while(node->untriedMoves.empty() && !node->children.empty()) {
                node = node->bestChild();
                if(node->move.x != -1) {
                    simRule->makeMove(node->move.x, node->move.y, simPlayer);
                }
                simPlayer = getOpponent(simPlayer);
            }
            
            // --- 2. Expansion (扩展) ---
            if(!node->untriedMoves.empty()) {
                int idx = rand() % node->untriedMoves.size();
                Point move = node->untriedMoves[idx];
                node->untriedMoves.erase(node->untriedMoves.begin() + idx);
                
                simRule->makeMove(move.x, move.y, simPlayer);
                MCTSNode* child = new MCTSNode(node, move, simPlayer, simBoard, simRule.get());
                node->children.push_back(child);
                node = child;
                simPlayer = getOpponent(simPlayer);
            }
            
            // --- 3. Simulation (模拟/Rollout) ---
            int depth = 0;
            while(depth < 60) { // 限制模拟深度，防止性能耗尽
                // 简单判断终局 (通用检查: 双方均无子可下)
                if (!simRule->hasValidMove(BLACK) && !simRule->hasValidMove(WHITE)) break;
                // 五子棋/黑白棋特定终局检查
                if (simRule->checkWin(0, 0) != PLAYING) break;

                // 寻找可行步
                vector<Point> moves;
                for(int i=0; i<simBoard.getSize(); ++i) 
                    for(int j=0; j<simBoard.getSize(); ++j) 
                        if(simRule->isValidMove(i, j, simPlayer)) moves.push_back({i, j});
                
                if(moves.empty()) {
                    simPlayer = getOpponent(simPlayer); // 虚着 Pass
                    continue;
                }
                
                // 随机落子
                Point randomMove = moves[rand() % moves.size()];
                simRule->makeMove(randomMove.x, randomMove.y, simPlayer);
                simPlayer = getOpponent(simPlayer);
                depth++;
            }
            
            // --- 4. Backpropagation (反向传播) ---
            double result = 0.0;
            GameStatus status = simRule->checkWin(0,0);
            
            // 如果没分出胜负(深度耗尽或无子可下)，强制计算分数
            if(status == PLAYING || status == DRAW) {
               float bScore, wScore;
               simRule->calculateScore(bScore, wScore);
               if(bScore > wScore) status = BLACK_WIN;
               else if(wScore > bScore) status = WHITE_WIN;
               else status = DRAW;
            }

            // 设定结果值：1.0 为黑胜，0.0 为白胜
            if(status == BLACK_WIN) result = 1.0; 
            else if(status == WHITE_WIN) result = 0.0;
            else result = 0.5;

            while(node != nullptr) {
                node->visits++;
                // MCTS 的关键：站在节点代表的棋手视角看胜负
                // 如果 node->playerMoved 是 BLACK，它希望结果是 1.0
                if(node->playerMoved == BLACK) node->wins += result;
                else node->wins += (1.0 - result); // 如果是 WHITE，它希望结果是 0.0
                node = node->parent;
            }
        }
        
        // 最终决策：选择访问次数最多的子节点 (最稳健)
        Point bestMove = {-1, -1};
        int maxVisits = -1;
        for(auto child : root->children) {
            if(child->visits > maxVisits) {
                maxVisits = child->visits;
                bestMove = child->move;
            }
        }
        
        cout << "MCTS 模拟次数: " << iterations << endl;
        delete root; // 清理内存
        return bestMove;
    }

    Point getMove(const Board& board, GameRule* rule, GameView* view) override {
        // Lv3 MCTS 调用
        if (level == 3) {
             cout << "AI (MCTS Lv3) 正在思考..." << endl;
             Point m = getMCTSMove(board, rule);
             if (m.x == -1) return {-1, -1}; // 无棋可走 Pass
             return m;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        
        vector<Point> validMoves;
        for(int i=0; i<board.getSize(); ++i) {
            for(int j=0; j<board.getSize(); ++j) {
                if(rule->isValidMove(i, j, color)) validMoves.push_back({i, j});
            }
        }

        if (validMoves.empty()) return {-1, -1}; 

        if (level == 1) { // Lv1 Random
            int idx = rand() % validMoves.size();
            return validMoves[idx];
        } else { // Lv2 Greedy
            int bestScore = -99999;
            Point bestMove = validMoves[0];
            
            // 黑白棋位置权值表 (贪心核心)
            int weights[8][8] = {
                {100, -20, 10,  5,  5, 10, -20, 100},
                {-20, -50, -2, -2, -2, -2, -50, -20},
                { 10,  -2, -1, -1, -1, -1,  -2,  10},
                {  5,  -2, -1, -1, -1, -1,  -2,   5},
                {  5,  -2, -1, -1, -1, -1,  -2,   5},
                { 10,  -2, -1, -1, -1, -1,  -2,  10},
                {-20, -50, -2, -2, -2, -2, -50, -20},
                {100, -20, 10,  5,  5, 10, -20, 100}
            };

            for (auto p : validMoves) {
                int score = 0;
                if (p.x < 8 && p.y < 8) score += weights[p.x][p.y];
                else score += 1; 
                score += rand() % 5; 
                if (score > bestScore) {
                    bestScore = score;
                    bestMove = p;
                }
            }
            return bestMove;
        }
    }
};

// ==========================================
// 6. Controller 层：游戏管理器
// ==========================================

struct GameState {
    Board board;
    PieceType currentPlayer;
    int passCount;
    vector<Point> moveHistory;
};

class GameManager {
private:
    unique_ptr<Board> board;
    unique_ptr<GameRule> rule;
    unique_ptr<GameView> view;
    unique_ptr<UserManager> userMgr;
    
    unique_ptr<Player> playerBlack;
    unique_ptr<Player> playerWhite;
    
    PieceType currentTurn;
    GameType gameType;
    int passCount;
    
    stack<GameState> undoStack;
    vector<Point> moveHistory;

    void saveState() {
        GameState state = {*board, currentTurn, passCount, moveHistory};
        undoStack.push(state);
    }

    // 辅助函数：根据 AI 等级返回名字
    string getAIName(int level) {
        if (level == 1) return "AI-Simple";
        if (level == 2) return "AI-Greedy";
        if (level == 3) return "AI-MCTS";
        return "AI";
    }

    void setupPlayers(int mode, string username) {
        if (mode == 1) { // PvP: 人人对战
            playerBlack = make_unique<HumanPlayer>(username, BLACK);
            playerWhite = make_unique<HumanPlayer>("Player2", WHITE);
        } 
        else if (mode == 2) { // PvAI: 人机对战
            int level = 1;
            string input = view->getUserInput("选择AI难度 (1:简单, 2:贪心, 3:MCTS): ");
            if (input == "2") level = 2;
            if (input == "3") level = 3;
            
            string side = view->getUserInput("你执黑吗? (y/n): ");
            if (side == "y") {
                playerBlack = make_unique<HumanPlayer>(username, BLACK);
                playerWhite = make_unique<AIPlayer>(getAIName(level) + "(W)", WHITE, level);
            } else {
                playerBlack = make_unique<AIPlayer>(getAIName(level) + "(B)", BLACK, level);
                playerWhite = make_unique<HumanPlayer>(username, WHITE);
            }
        } 
        else { // AIvAI: 机机对战
            // 允许分别设置黑白双方的 AI 难度
            int levelBlack = 1;
            string inputB = view->getUserInput("选择黑方AI难度 (1:简单, 2:贪心, 3:MCTS): ");
            if (inputB == "2") levelBlack = 2;
            if (inputB == "3") levelBlack = 3;

            int levelWhite = 1;
            string inputW = view->getUserInput("选择白方AI难度 (1:简单, 2:贪心, 3:MCTS): ");
            if (inputW == "2") levelWhite = 2;
            if (inputW == "3") levelWhite = 3;

            playerBlack = make_unique<AIPlayer>(getAIName(levelBlack) + "(B)", BLACK, levelBlack);
            playerWhite = make_unique<AIPlayer>(getAIName(levelWhite) + "(W)", WHITE, levelWhite);
        }
    }

public:
    GameManager() {
        view = make_unique<ConsoleView>();
        userMgr = make_unique<UserManager>();
        srand(time(0));
    }

    void run() {
        while (true) {
            if (!userMgr->isLoggedIn()) {
                view->showMainMenu();
                string choice = view->getUserInput("请选择: ");
                if (choice == "1") {
                    string u = view->getUserInput("用户名: ");
                    string p = view->getUserInput("密码: ");
                    if (userMgr->login(u, p)) cout << "登录成功！" << endl;
                    else cout << "登录失败。" << endl;
                } else if (choice == "2") {
                    string u = view->getUserInput("用户名: ");
                    string p = view->getUserInput("密码: ");
                    if (userMgr->registerUser(u, p)) cout << "注册成功，请登录。" << endl;
                    else cout << "用户已存在。" << endl;
                } else if (choice == "3") {
                    cout << "以游客身份进入。" << endl;
                    break; 
                } else if (choice == "4") {
                    return;
                }
                if (userMgr->isLoggedIn()) break;
            } else break;
        }

        while(true) {
            cout << "\n欢迎, " << userMgr->getStats(userMgr->getCurrentUsername()) << endl;
            cout << "1. 开始游戏" << endl;
            cout << "2. 读取存档/回放" << endl;
            cout << "3. 退出登录" << endl;
            string choice = view->getUserInput("请选择: ");

            if (choice == "3") {
                userMgr->logout();
                run(); 
                return;
            } else if (choice == "1") {
                cout << "选择游戏: 1.五子棋 2.围棋 3.黑白棋" << endl;
                string g = view->getUserInput("> ");
                if (g == "1") gameType = GOMOKU;
                else if (g == "2") gameType = GO;
                else gameType = REVERSI;

                int size = (gameType == REVERSI) ? 8 : 15;
                if (gameType == GO) size = 19;
                
                cout << "选择模式: 1.人人对战 2.人机对战 3.机机对战" << endl;
                string m = view->getUserInput("> ");
                
                board = make_unique<Board>(size);
                if (gameType == GOMOKU) rule = make_unique<GomokuRule>(board.get());
                else if (gameType == GO) rule = make_unique<GoRule>(board.get());
                else rule = make_unique<ReversiRule>(board.get());
                
                rule->initBoard();
                setupPlayers(stoi(m), userMgr->getCurrentUsername());
                
                currentTurn = BLACK;
                passCount = 0;
                moveHistory.clear();
                while(!undoStack.empty()) undoStack.pop();

                gameLoop();
            } else if (choice == "2") {
                string fname = view->getUserInput("输入文件名: ");
                if (loadGame(fname)) {
                    cout << "1. 继续游戏  2. 观看回放" << endl;
                    if (view->getUserInput("> ") == "2") {
                        replayMode();
                    } else {
                        gameLoop();
                    }
                } else {
                    cout << "读取失败。" << endl;
                }
            }
        }
    }

    void gameLoop() {
        bool running = true;
        while (running) {
            Player* p = (currentTurn == BLACK) ? playerBlack.get() : playerWhite.get();
            string msg = "轮到 " + p->getName() + " (" + (currentTurn==BLACK?"黑":"白") + ")";
            view->displayBoard(*board, currentTurn, msg);

            if (rule->supportsPass()) {
                if (!rule->hasValidMove(currentTurn)) {
                    cout << "无子可下，被迫弃权 (Pass)!" << endl;
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    saveState();
                    passCount++;
                    moveHistory.push_back({-1, -1}); 
                    if (passCount >= 2 || (gameType==REVERSI && board->countPieces(EMPTY)==0)) {
                         goto GAME_OVER;
                    }
                    currentTurn = getOpponent(currentTurn);
                    continue;
                }
            }

            Point move = p->getMove(*board, rule.get(), view.get());

            if (move.x == -2) { // Undo
                if (undoStack.empty()) { cout << "无法悔棋" << endl; continue; }
                GameState prev = undoStack.top(); undoStack.pop();
                *board = prev.board; currentTurn = prev.currentPlayer; passCount = prev.passCount; moveHistory = prev.moveHistory;
                continue;
            }
            if (move.x == -3) { // Save
                string fname = view->getUserInput("输入文件名: ");
                saveGame(fname);
                continue;
            }
            if (move.x == -4) { // Quit
                running = false; break;
            }
            if (move.x == -1) { // Manual Pass
                if (gameType != GO) { cout << "此游戏不支持主动虚着" << endl; continue; }
                saveState();
                passCount++;
                moveHistory.push_back({-1, -1});
                currentTurn = getOpponent(currentTurn);
                if (passCount >= 2) goto GAME_OVER;
                continue;
            }

            if (rule->isValidMove(move.x, move.y, currentTurn)) {
                saveState();
                rule->makeMove(move.x, move.y, currentTurn);
                moveHistory.push_back(move);
                passCount = 0;
                
                GameStatus status = rule->checkWin(move.x, move.y);
                
                if (gameType == REVERSI) {
                    if (board->countPieces(EMPTY) == 0 || 
                        board->countPieces(BLACK) == 0 || 
                        board->countPieces(WHITE) == 0) {
                        status = rule->checkWin(move.x, move.y); 
                    } else if (!rule->hasValidMove(BLACK) && !rule->hasValidMove(WHITE)) {
                         status = rule->checkWin(move.x, move.y);
                    }
                }

                if (status != PLAYING) {
                    view->displayBoard(*board, currentTurn, "游戏结束!");
                    if (status == BLACK_WIN) {
                        cout << "黑方获胜!" << endl;
                        if (!playerBlack->isAI()) userMgr->recordGameResult(true);
                        if (!playerWhite->isAI()) userMgr->recordGameResult(false);
                    } else if (status == WHITE_WIN) {
                        cout << "白方获胜!" << endl;
                        if (!playerWhite->isAI()) userMgr->recordGameResult(true);
                        if (!playerBlack->isAI()) userMgr->recordGameResult(false);
                    } else {
                        cout << "平局!" << endl;
                        if (!playerBlack->isAI()) userMgr->recordGameResult(false);
                        if (!playerWhite->isAI()) userMgr->recordGameResult(false);
                    }
                    running = false;
                    view->getUserInput("按回车返回...");
                } else {
                    currentTurn = getOpponent(currentTurn);
                }
            } else {
                if (!p->isAI()) cout << "落子不合法!" << endl;
            }
        }
        return;

    GAME_OVER:
        view->displayBoard(*board, currentTurn, "游戏结束 (双人虚着/无子可下)!");
        float bScore = 0, wScore = 0;
        rule->calculateScore(bScore, wScore);
        cout << fixed << setprecision(2);
        cout << "黑方: " << bScore << ", 白方: " << wScore << endl;
        if (bScore > wScore) {
            cout << "黑方获胜!" << endl;
            if (!playerBlack->isAI()) userMgr->recordGameResult(true);
        } else {
            cout << "白方获胜!" << endl;
            if (!playerWhite->isAI()) userMgr->recordGameResult(true);
        }
        view->getUserInput("按回车返回...");
    }

    void replayMode() {
        board->clear();
        rule->initBoard();
        
        cout << "=== 进入回放模式 ===" << endl;
        cout << "总步数: " << moveHistory.size() << endl;
        
        PieceType p = BLACK;
        for (size_t i = 0; i < moveHistory.size(); ++i) {
            view->displayBoard(*board, p, "回放中... (回车下一步, q退出)");
            string cmd = view->getUserInput("");
            if (cmd == "q") break;

            Point m = moveHistory[i];
            if (m.x == -1) {
                cout << "Step " << i+1 << ": Pass" << endl;
            } else {
                rule->makeMove(m.x, m.y, p);
            }
            p = getOpponent(p);
        }
        cout << "回放结束。" << endl;
        view->getUserInput("按回车返回...");
    }

    void saveGame(string filename) {
        ofstream file(filename);
        file << (int)gameType << " " << (int)currentTurn << " " << passCount << endl;
        file << board->serialize() << endl;
        file << moveHistory.size() << endl;
        for(auto p : moveHistory) file << p.x << " " << p.y << " ";
        file.close();
        cout << "存档成功!" << endl;
    }

    bool loadGame(string filename) {
        ifstream file(filename);
        if (!file.is_open()) return false;
        int gt, ct;
        file >> gt >> ct >> passCount;
        gameType = (GameType)gt;
        currentTurn = (PieceType)ct;
        
        stringstream ss;
        string dummy; getline(file, dummy); 
        string boardStr; getline(file, boardStr);
        ss << boardStr;
        
        int size;
        if (gameType == REVERSI) size = 8;
        else if (gameType == GO) size = 19;
        else size = 15;
        
        board = make_unique<Board>(size); 
        board->deserialize(ss); 
        
        if (gameType == GOMOKU) rule = make_unique<GomokuRule>(board.get());
        else if (gameType == GO) rule = make_unique<GoRule>(board.get());
        else rule = make_unique<ReversiRule>(board.get());

        int histSize;
        file >> histSize;
        moveHistory.clear();
        for(int i=0; i<histSize; ++i) {
            int x, y; file >> x >> y;
            moveHistory.push_back({x, y});
        }
        
        setupPlayers(1, userMgr->getCurrentUsername()); 
        
        return true;
    }
};

int main() {
    GameManager game;
    game.run();
    return 0;
}
