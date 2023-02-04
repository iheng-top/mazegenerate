#include <iostream>
#include <string>
#include <string_view>
#include <array>
#include <vector>
#include <random>
#include <chrono>
#include <functional>
#include <algorithm>
#include <optional>
#include <thread>
#include <exception>

#ifdef _WIN32
#	include <windows.h>
#else
#	include <unistd.h>
#	include <sys/types.h>
#	include <sys/ioctl.h>
#	include <termios.h>
#endif


// 基类迷宫，抽象类，子类重写generate方法生成迷宫
// 使用二维数组存储，rows行，cols列，最外围一圈是边界(border)，中间是墙(wall)或通道(passage)
class BaseMaze
{
public:
	enum class CellType;
	
    BaseMaze(int rows, int cols, CellType defaultType=CellType::WALL): 
        rows(rows),
        cols(cols),
        entryCell(1, 1, CellType::ENTRY),
        exportCell(rows - 2, cols - 2, CellType::EXPORT),
		defaultType(defaultType)
    {
		// 获取当前终端的大小(行数和列数)
#ifdef _WIN32
		CONSOLE_SCREEN_BUFFER_INFO info;
		GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info);
		screenRows = info.srWindow.Bottom - info.srWindow.Top + 1;
		screenCols = info.srWindow.Right - info.srWindow.Left + 1;
#else
		struct winsize size;
		ioctl(STDIN_FILENO, TIOCGWINSZ, &size);
		screenRows = size.ws_row;
		screenCols = size.ws_col;
#endif
		
		if (rows < 5 || cols < 5 || rows % 2 == 0 || cols % 2 == 0) {
			throw std::invalid_argument("The rows and columns must be odd numbers greater than 3.");
		}
		
		// 必须在终端中运行，确保窗口大小能容纳迷宫
		if (rows > screenRows - 2 || cols * 2 > screenCols) {
			throw std::invalid_argument("The rows or columns exceeds the terminal display range.");
		}

        board.resize(rows);
        for (auto &row : board) {
            row.resize(cols);
        }

        for (int i = 0; i < rows; ++i) {
            for (int j = 0; j < cols; ++j) {
                auto &cell = board[i][j];
                if (i == 0 || i == rows - 1 || j == 0 || j == cols - 1) {
                    cell = CellType::BORDER;
                }
                else {
                    cell = defaultType;
                }
            }
        }
		
		upsetEndPoint();
		updateEndPoint();
    }
	
	// 动态演示走迷宫的过程：递归，深度优先搜索
    void travelMaze() {
		// travel: 递归
        std::function<bool(int row, int col)> travel = [this, &travel](int row, int col)->bool {
			// updateMaze: 更新新探索的位置的状态
			auto updateMaze = [this](int row, int col, std::string_view s) {
				board[row][col] = CellType::CURRENT;
				cursorTo(row, col, "\033[31mo \033[0m");
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				if (!isExport(row, col)) {
					board[row][col] = CellType::VISITED;
					cursorTo(row, col, s);
				}
			};	// updateMaze function end
			
			// nextTo: 递归探索 direction
			auto nextTo = [row, col, this, &updateMaze, &travel](Direction direction) {
				int nextRow = row, nextCol = col;
				switch (direction) {
				case Direction::UP:
					nextRow -= 1;
					break;
				case Direction::DOWN:
					nextRow += 1;
					break;
				case Direction::LEFT:
					nextCol -= 1;
					break;
				case Direction::RIGHT:
					nextCol += 1;
					break;
				default:
					break;
				}
				if (isPassage(nextRow, nextCol)) {
					if (!travel(nextRow, nextCol)) {	// 回溯阶段使用紫色的星号标注
						updateMaze(nextRow, nextCol, "\033[35m* \033[0m");
					}
					else {	// 找到终点后的回溯路径，使用红色的加号标注，标注处即为起点到终点的唯一路径
						cursorTo(row, col, "\033[31m+ \033[0m");
						return true;
					}
				}
				return false;
			};	// nextTo function end

			// 递推阶段，使用黄色星号标注探索的路径
			updateMaze(row, col, "\033[33m* \033[0m");
			// 递归出口
            if (isExport(row, col)) {
                return true;
            }
			// 根据当前位置到终点的距离设置下一步探索的顺序
			using EleType = std::pair<Direction, int>;
			std::array<EleType, 4> directions {
				std::pair(Direction::UP, std::abs(row - 1 - exportCell.row) + std::abs(col - exportCell.col)),
				std::pair(Direction::DOWN, std::abs(row + 1 - exportCell.row) + std::abs(col - exportCell.col)),
				std::pair(Direction::LEFT, std::abs(row - exportCell.row) + std::abs(col - 1 - exportCell.col)),
				std::pair(Direction::RIGHT, std::abs(row - exportCell.row) + std::abs(col + 1 - exportCell.col)),
			};
			// 排序后按照距离非递减的顺序排列
			std::sort(std::begin(directions), std::end(directions), [](const EleType &a, const EleType &b) {
					return a.second < b.second;
				});
			
			// 优先探索距离终点更近的方向
			if (nextTo(directions[0].first)) {
				return true;
			}
			if (nextTo(directions[1].first)) {
				return true;
			}
			if (nextTo(directions[2].first)) {
				return true;
			}
			if (nextTo(directions[3].first)) {
				return true;
			}
			// 四个方向都无法前进开始回溯
			return false;
        };	// trave funtion end

		// travelMaze start
		// 更新时直接将光标移动到指定单元格的位置
		hideCursor(true);
		std::cout << *this << std::endl;
		// 递归函数调用
        travel(entryCell.row, entryCell.col);

		cursorTo(rows, 0);
		hideCursor(false);
    }
	
	// 打乱起点和终点的坐标位置，默认位置在左上和右下角
	void upsetEndPoint() {
		std::mt19937 mt = makeMt();
		std::uniform_int_distribution directionDis(1, 4);
		std::uniform_int_distribution rowDis(1, cols - 2);
		std::uniform_int_distribution colDis(1, rows - 2);
		
		auto upset = [&, this](Cell &cell) {
			const int direction = directionDis(mt);
			const int rpos = rowDis(mt) / 2 * 2 + 1;
			const int cpos = colDis(mt) / 2 * 2 + 1;
			switch (direction) {
			case 1:
				cell.row = 1;
				cell.col = rpos;
				break;
			case 2:
				cell.row = rows - 2;
				cell.col = rpos;
				break;
			case 3:
				cell.row = cpos;
				cell.col = 1;
				break;
			case 4:
				cell.row = cpos;
				cell.col = cols - 2;
				break;
			default:
				break;
			}
		};
		
		do {
			upset(entryCell);
			upset(exportCell);
		} while (entryCell.row == exportCell.row && entryCell.col == exportCell.col);
	}

	// 绘制起点和终点的位置；
	// 为了方便，起点和终点绘制在边界上，而extryCell和exportCell在边界内部
	void updateEndPoint() {
		auto update = [this](int row, int col, CellType type) {
			if (col == 1) {
				board[row][0] = type;
			}
			else if (col == cols - 2) {
				board[row][cols - 1] = type;
			}
			else if (row == 1) {
				board[0][col] = type;
			}
			else if (row == rows - 2) {
				board[rows - 1][col] = type;
			}
		};
		update(entryCell.row, entryCell.col, CellType::ENTRY);
		update(exportCell.row, exportCell.col, CellType::EXPORT);
	}
	
	bool isInMaze(int row, int col) const {
		return row > 0 && row < rows - 1 && col > 0 && col < cols - 1;
	}
	
	bool isPassage(int row, int col) const {
		return isInMaze(row, col) && board[row][col] == CellType::PASSAGE;
	}
	
	bool isWall(int row, int col) const {
		return isInMaze(row, col) && board[row][col] == CellType::WALL;
	}
	
	bool isExport(int row, int col) const {
		return row == exportCell.row && col == exportCell.col;
	}

	bool isEntry(int row, int col) const {
		return row == entryCell.row && col == entryCell.col;
	}
	
	void setPassage(int row, int col) {
		board[row][col] = CellType::PASSAGE;
	}
	
	void setWall(int row, int col) {
		board[row][col] = CellType::WALL;
	}
	
	static std::mt19937 makeMt() {
		using namespace std::chrono;
		std::random_device rd;
		int seed = rd.entropy() ? rd() : duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
		return std::mt19937(seed);
	}
	
	static void hideCursor(bool hide) {
		std::cout << (hide ? "\033[?25l" : "\033[?25h");
	}
	
	static void cursorTo(int row, int col, std::string_view s="") {
		// Linux 下缓冲区刷新不及时，需要强制刷新
		std::cout << "\033[" << row + 1 << ';' << (col * 2) + 1 << 'H' << s << std::flush;
	}
	
	static void clear() {
#ifdef _WIN32
		// Windows 下测试时清屏指令 "\033[2J" 存在问题
		system("cls");
#else
		std::cout << "\033[2J\033[1;1H";
#endif
	}
	
	virtual void generate() = 0;

    friend std::ostream &operator<<(std::ostream &out, const BaseMaze &maze);

public:
    enum class CellType {
        BORDER,
        WALL,
        PASSAGE,
        ENTRY,
        EXPORT,
        CURRENT,
        VISITED
    };

    enum class Direction {
        UP,
        DOWN,
        LEFT,
        RIGHT
    };

    struct Cell {
        int row;
        int col;
        CellType type;
        Cell(int r = 0, int c = 0, CellType t = CellType::WALL): row(r), col(c), type(t) {}
    };
	
protected:
	int rows;
	int cols;
    Cell entryCell;
    Cell exportCell;

    std::vector<std::vector<CellType>> board;

private:
	int screenRows;
	int screenCols;
	CellType defaultType;
};


std::ostream &operator<<(std::ostream &out, const BaseMaze &maze) 
{
	BaseMaze::clear();
    for (const auto &rows : maze.board) {
        for (const auto &cell : rows) {
            switch (cell) {
            case BaseMaze::CellType::BORDER:
            case BaseMaze::CellType::WALL:
                out << "\033[7m  \033[0m";
                break;
            case BaseMaze::CellType::PASSAGE:
                out << "  ";
                break;
            case BaseMaze::CellType::ENTRY:
                out << "\033[7;32mI \033[0m";
                break;
            case BaseMaze::CellType::EXPORT:
                out << "\033[7;32mO \033[0m";
                break;
			case BaseMaze::CellType::VISITED:
                out << "\033[33m* \033[0m";
                break;
            case BaseMaze::CellType::CURRENT:
				out << "\033[31mo \033[0m";
                break;
            default:
                break;
            }
        }
        out << std::endl;
    }
    return out;
}


// 算法一 主路型迷宫：深度优先，递归实现
// 迷宫中会形成一条很长的主路，主路长且扭曲，岔路较少

class MainRoadMaze: public BaseMaze
{
public:
    MainRoadMaze(int rows = 21, int cols = 21): 
        BaseMaze(rows, cols) {
			generate();
		}

    void generate() override {
        std::mt19937 mt = makeMt();

        auto getDirection = [this, &mt](int row, int col) ->std::optional<Direction> {
            std::vector<Direction> directions;
            if (isWall(row - 2, col)) {
                directions.push_back(Direction::UP);
            }
            if (isWall(row + 2, col)) {
                directions.push_back(Direction::DOWN);
            }
            if (isWall(row, col - 2))  {
                directions.push_back(Direction::LEFT);
            }
            if (isWall(row, col + 2)) {
                directions.push_back(Direction::RIGHT);
            }
            if (directions.empty()) {
                return std::nullopt;
            }
            int choice = mt() % directions.size();
            return directions[choice];  
        };

        std::function<void(int, int)> getThrougn = [this, &getThrougn, &getDirection](int row, int col) {
            std::optional<Direction> direction;
            while ((direction = getDirection(row, col)).has_value()) {
                switch (direction.value()) {
                case Direction::UP:
					setPassage(row - 1, col);
					setPassage(row - 2, col);
                    row -= 2;
                    break;
                case Direction::DOWN:
					setPassage(row + 1, col);
					setPassage(row + 2, col);
                    row += 2;
                    break;
                case Direction::LEFT:
					setPassage(row, col - 1);
					setPassage(row, col - 2);
                    col -= 2;
                    break;
                case Direction::RIGHT:
					setPassage(row, col + 1);
					setPassage(row, col + 2);
                    col += 2;
                    break;
                default:
                    break;
                }
                getThrougn(row, col);
            }
        };

        int curRow = entryCell.row, curCol = entryCell.col;
        setPassage(curRow, curCol);
        getThrougn(curRow, curCol);
    }
};


// 算法二 自然分叉型迷宫：类似广度优先生成
// 没有明显的主路，死胡同很多
class NatualMaze: public BaseMaze
{
public:
    NatualMaze(int rows = 21, int cols = 21): 
        BaseMaze(rows, cols) { 
			generate(); 
		}

    void generate() override {
        std::mt19937 mt = makeMt();

        std::vector<Point> expansions;

        auto addExps = [this, &expansions](int row, int col) {
            if (isWall(row - 1, col)) {
                expansions.emplace_back(row - 1, col, Direction::UP);
            }
            if (isWall(row + 1, col)) {
                expansions.emplace_back(row + 1, col, Direction::DOWN);
            }
            if (isWall(row, col - 1))  {
                expansions.emplace_back(row, col - 1, Direction::LEFT);
            }
            if (isWall(row, col + 1)) {
                expansions.emplace_back(row, col + 1, Direction::RIGHT);
            }
        };

        int curRow = entryCell.row, curCol = entryCell.row;
        setPassage(curRow, curCol);
        addExps(curRow, curCol);

        while (!expansions.empty()) {
            const int choiceExp = mt() % expansions.size();
            const auto &selectedPoint = expansions[choiceExp];
            curRow = selectedPoint.row;
            curCol = selectedPoint.col;

            switch (selectedPoint.direction) {
            case Direction::UP:
                curRow -= 1;
                break;
            case Direction::DOWN:
                curRow += 1;
                break;
            case Direction::LEFT:
                curCol -= 1;
                break;
            case Direction::RIGHT:
                curCol += 1;;
                break;
            default:
                break;
            }

            if (isWall(curRow, curCol)) {
				setPassage(selectedPoint.row, selectedPoint.col);
				setPassage(curRow, curCol);

                addExps(curRow, curCol);
            }

            expansions.erase(expansions.begin() + choiceExp);
        }
    }

public:
    struct Point {
        int row;
        int col;
        Direction direction;

        Point(int r = 0, int c = 0, Direction d = Direction::UP): row(r), col(c), direction(d) {}
    };
};


// 算法三 简单型迷宫：分治法，递归
// 容易形成很多长直通道
class SimpleMaze: public BaseMaze
{
public:
	SimpleMaze(int rows = 21, int cols = 21):
		BaseMaze(rows, cols, CellType::PASSAGE) {
			generate();
		}
		
	void generate() override {
		std::mt19937 mt = makeMt();
		
		std::function<void(int, int, int, int)> devide = [this, &devide, &mt](int t, int b, int l, int r) {
			if (t != b && l != r) {
				int crossPointRow = t + mt() % ((b - t) / 2) * 2 + 1;
				int crossPointCol = l + mt() % ((r - l) / 2) * 2 + 1;
				
				for (int i = l; i <= r; ++i) {
					board[crossPointRow][i] = CellType::WALL;
				}
				for (int i = t; i <= b; ++i) {
					board[i][crossPointCol] = CellType::WALL;
				}
				
				int notThrough = mt() % 4;
				std::array<int, 4> pos = {
					static_cast<int>(t + mt() % (crossPointRow - t) / 2 * 2), 
					static_cast<int>(b - mt() % (b - crossPointRow) / 2 * 2),
					static_cast<int>(l + mt() % (crossPointCol - l) / 2 * 2),
					static_cast<int>(r - mt() % (r - crossPointCol) / 2 * 2)
				};
				board[pos[0]][crossPointCol] = CellType::PASSAGE;
				board[pos[1]][crossPointCol] = CellType::PASSAGE;
				board[crossPointRow][pos[2]] = CellType::PASSAGE;
				board[crossPointRow][pos[3]] = CellType::PASSAGE;
				switch (notThrough) {
				case 0:
					board[pos[0]][crossPointCol] = CellType::WALL;
					break;
				case 1:
					board[pos[1]][crossPointCol] = CellType::WALL;
					break;
				case 2:
					board[crossPointRow][pos[2]] = CellType::WALL;
					break;
				case 3:
					board[crossPointRow][pos[3]] = CellType::WALL;
					break;
				default:
					break;
				}
				devide(t, crossPointRow - 1, l, crossPointCol - 1);
				devide(crossPointRow + 1, b, l, crossPointCol - 1);
				devide(crossPointRow + 1, b, crossPointCol + 1, r);
				devide(t, crossPointRow - 1, crossPointCol + 1, r);
			}
		};
		
		devide(1, rows - 2, 1, cols - 2);
	}

};


// maze [<mainroad|natual|simple> [<rows> [<cols>]]]
// maze simple
// maze natual 11
// maze mainroad 17 27
int main(int argc, char* args[])
{
	if (argc < 2) {
		NatualMaze maze;
		std::cout << maze << std::endl;
		maze.travelMaze();
	}
	else {
		int rows = 21, cols = 21;
		if (argc == 3) {
			rows = std::atoi(args[2]);
			cols = rows;
		}
		else if (argc == 4) {
			rows = std::atoi(args[2]);
			cols = std::atoi(args[3]);
		}
		if (std::string("mainroad") == args[1]) {
			MainRoadMaze maze(rows, cols);
			std::cout << maze << std::endl;
			maze.travelMaze();
		}
		else if (std::string("natual") == args[1]) {
			NatualMaze maze(rows, cols);
			std::cout << maze << std::endl;
			maze.travelMaze();
		}
		else if (std::string("simple") == args[1]) {
			SimpleMaze maze(rows, cols);
			std::cout << maze << std::endl;
			maze.travelMaze();
		}
		else {
			std::clog << "Usage: maze <simple/mainroad/natual> <rows> <cols>\n";
			return -1;
		}
	}
    return 0;
}

