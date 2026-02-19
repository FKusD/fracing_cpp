# 🚗 План развития 2D автосимулятора

## Текущий статус ✅
- [x] Базовая физика автомобиля (bicycle model)
- [x] Box2D интеграция для коллизий
- [x] Телеметрия (скорость, позиция, угол)
- [x] Базовая система управления (WASD + Space)
- [x] Исправленная управляемость и физика столкновений
- [x] Улучшенный зум камеры

---

## ФАЗА 1: Улучшение управляемости и визуализации (1-2 недели)

### 1.1 Доработка физики (Приоритет: ВЫСОКИЙ)
- [ ] **Плавное руление** - добавить интерполяцию угла поворота колес
    - Текущее: мгновенный поворот при нажатии A/D
    - Цель: плавное ускорение угла руля (параметр `steerResponseSpeed`)
    - Файл: `VehicleModel.cpp`

- [ ] **Продвинутая модель шин** - опционально, на основе Pacejka (уже есть PDF!)
    - Файл: `/mnt/project/Pacejka_H_B_-_Tyre_and_Vehicle_Dynamics_-_2007.pdf`
    - Реализовать упрощенную Magic Formula для более реалистичного поведения

- [ ] **Дифференциальная блокировка** - разные скорости вращения колес при повороте
    - Сейчас: bicycle model (одна скорость)
    - Цель: учет разницы в радиусе поворота внутреннего/внешнего колеса

### 1.2 Визуальные улучшения (Приоритет: СРЕДНИЙ)
- [ ] **Улучшенная графика машины**
    - Заменить треугольник на спрайт или простую 3D модель
    - Добавить анимацию поворота колес

- [ ] **Следы от шин**
    - Отрисовка линий при дрифте (когда `muSurface` низкое)
    - Fade-out эффект со временем

- [ ] **Частицы**
    - Пыль при дрифте
    - Искры при столкновении

- [ ] **Миникарта**
    - Показывать всю трассу в углу экрана
    - Точка - позиция игрока

### 1.3 Динамический зум (Приоритет: НИЗКИЙ)
- [ ] Управление зумом через колесико мыши или +/-
- [ ] Автоматический зум в зависимости от скорости
    - На низкой скорости - ближе
    - На высокой - дальше (больше видно дорогу)

---

## ФАЗА 2: Редактор трассы (2-3 недели)

### 2.1 Базовый редактор (Приоритет: ВЫСОКИЙ)
**Цель**: Создать и сохранять собственные трассы

#### Архитектура:
```
TrackEditor
├── EditMode (bool) - режим редактирования
├── PlacementMode (enum) - что размещаем
│   ├── WALL - стены трассы
│   ├── CHECKPOINT - чекпоинты
│   ├── SPAWN_POINT - стартовая точка
│   └── OBSTACLE - препятствия
├── CurrentWall - активная линия стены
└── Tools
    ├── AddPoint(x, y) - добавить точку в стену
    ├── DeleteLastPoint() - удалить последнюю точку
    ├── FinishWall() - завершить текущую стену
    └── Clear() - очистить всё
```

#### Функционал:
- [ ] **Режим редактирования** (F3 или кнопка в меню)
    - Пауза симуляции
    - Показ сетки (grid)
    - Отображение координат мыши

- [ ] **Рисование стен**
    - ЛКМ - добавить точку стены
    - ПКМ - завершить текущую стену
    - Backspace - удалить последнюю точку
    - Визуальная индикация активной стены (другой цвет)

- [ ] **Размещение объектов**
    - 1 - режим стен
    - 2 - режим чекпоинтов
    - 3 - режим стартовой позиции
    - Визуальные иконки для каждого объекта

- [ ] **Параметры стен**
    - Толщина (регулируется колесиком мыши)
    - Трение (slider в UI)
    - Restitution/отскок (slider в UI)

### 2.2 Сохранение и загрузка (Приоритет: ВЫСОКИЙ)

#### Формат файла: JSON
```json
{
  "track_name": "Моя трасса",
  "version": "1.0",
  "spawn": {
    "x": 0.0,
    "y": 0.0,
    "yaw_deg": 0.0
  },
  "walls": [
    {
      "vertices": [[x1, y1], [x2, y2], ...],
      "thickness": 0.5,
      "friction": 0.35,
      "restitution": 0.25
    }
  ],
  "checkpoints": [
    {
      "position": [x, y],
      "width": 10.0,
      "angle_deg": 90.0
    }
  ]
}
```

**Библиотека**: nlohmann/json (header-only, легко интегрируется)

#### Функции:
- [ ] `saveTrack(filename)` - сохранить в JSON
- [ ] `loadTrack(filename)` - загрузить из JSON
- [ ] `validateTrack()` - проверка корректности трассы
    - Есть стартовая точка
    - Есть хотя бы 1 стена
    - Нет пересекающихся стен

- [ ] **Автосохранение** каждые 60 секунд в `autosave.json`

### 2.3 UI для редактора (Приоритет: СРЕДНИЙ)
Используем ImGui (уже интегрирован в проект!)

```cpp
// Пример окна редактора
ImGui::Begin("Track Editor");

if (ImGui::Button("New Track")) { ... }
if (ImGui::Button("Load Track")) { ... }
if (ImGui::Button("Save Track")) { ... }

ImGui::Separator();
ImGui::Text("Mode:");
if (ImGui::RadioButton("Walls", mode == WALL)) { mode = WALL; }
if (ImGui::RadioButton("Checkpoints", mode == CHECKPOINT)) { mode = CHECKPOINT; }
if (ImGui::RadioButton("Spawn", mode == SPAWN)) { mode = SPAWN; }

ImGui::Separator();
ImGui::SliderFloat("Wall Thickness", &wallThickness, 0.1f, 2.0f);
ImGui::SliderFloat("Friction", &wallFriction, 0.0f, 1.0f);

ImGui::End();
```

- [ ] Окно свойств трассы
- [ ] Окно списка объектов (можно кликнуть и выделить)
- [ ] Окно режимов рисования
- [ ] Кнопки сохранения/загрузки
- [ ] Статистика трассы (количество стен, длина, площадь)

### 2.4 Продвинутые функции (Приоритет: НИЗКИЙ)
- [ ] **Копирование/вставка стен** (Ctrl+C / Ctrl+V)
- [ ] **Отмена/повтор** (Undo/Redo) - стек команд
- [ ] **Симметрия** - автоматическое зеркалирование (для симметричных трасс)
- [ ] **Сплайны** - сглаживание углов стен (Catmull-Rom или Bezier)
- [ ] **Текстуры поверхности** - разные зоны с разным `muSurface`
    - Асфальт (μ = 1.2)
    - Грязь (μ = 0.6)
    - Лед (μ = 0.3)

---

## ФАЗА 3: Система чекпоинтов и таймер (1 неделя)

### 3.1 Чекпоинты (Приоритет: ВЫСОКИЙ)
```cpp
struct Checkpoint {
    glm::vec2 position;
    float width;        // ширина чекпоинта
    float angle;        // угол ориентации
    int index;          // порядковый номер
    bool isPassed;      // пройден ли
};
```

- [ ] Определение пересечения машины с чекпоинтом
- [ ] Визуализация чекпоинтов (линия или арка)
- [ ] Система прогресса (пройдено 3/10 чекпоинтов)
- [ ] Проверка правильной последовательности (нельзя пропустить)

### 3.2 Таймер и круги (Приоритет: ВЫСОКИЙ)
- [ ] Засечка времени круга
- [ ] Сохранение лучшего времени
- [ ] Отображение текущего/лучшего времени
- [ ] Секторы (split times) - промежуточные результаты
- [ ] Призрак лучшего круга (ghost replay)

---

## ФАЗА 4: Интерфейс для обучения нейросети (3-4 недели)

### 4.1 Архитектура обучения

#### Выбор подхода:
**Рекомендую: Reinforcement Learning (RL)**
- Библиотека: **Stable-Baselines3** (Python) или **libtorch** (C++)
- Алгоритм: **PPO** (Proximal Policy Optimization) или **SAC** (Soft Actor-Critic)

#### Альтернатива: Supervised Learning
- Запись человеческих заездов
- Обучение модели повторять действия
- Проще, но менее гибко

### 4.2 API для Python (Приоритет: ВЫСОКИЙ)

**Вариант A: C++ сервер + Python клиент** (Рекомендуемый)

```cpp
// Headless режим - без рендеринга
class SimulationServer {
    void reset();
    State step(Action action);
    bool isDone();
    float getReward();
};

// HTTP/WebSocket сервер
Server server;
server.post("/reset", [](){ ... });
server.post("/step", [](json action){ ... });
server.get("/state", [](){ ... });
```

**Вариант B: Python bindings через pybind11**
```cpp
PYBIND11_MODULE(car_sim, m) {
    py::class_<CarGame>(m, "CarGame")
        .def(py::init<>())
        .def("reset", &CarGame::reset)
        .def("step", &CarGame::step)
        .def("get_state", &CarGame::getState);
}
```

#### Преимущества варианта A:
- Не нужно перекомпилировать C++ при изменении Python кода
- Можно тренировать на другой машине
- Легко масштабировать (несколько экземпляров)

#### Преимущества варианта B:
- Быстрее (нет сетевого оверхеда)
- Проще дебажить

### 4.3 State space (что видит агент)

**Минимальный набор (9 параметров):**
```python
state = [
    velocity_x,           # скорость по X
    velocity_y,           # скорость по Y
    angular_velocity,     # угловая скорость
    angle_to_next_cp,     # угол до следующего чекпоинта
    distance_to_next_cp,  # расстояние до чекпоинта
    distance_to_wall_front,   # расстояние до стены спереди (ray)
    distance_to_wall_left,    # слева
    distance_to_wall_right,   # справа
    current_speed         # общая скорость
]
```

**Продвинутый набор (с lidar):**
```python
state = [
    ...базовые параметры...,
    lidar_readings[0..15]  # 16 лучей вокруг машины
]
```

### 4.4 Action space (что делает агент)

**Дискретные действия (проще):**
```python
actions = {
    0: NO_ACTION,
    1: THROTTLE,
    2: BRAKE,
    3: STEER_LEFT,
    4: STEER_RIGHT,
    5: THROTTLE + STEER_LEFT,
    6: THROTTLE + STEER_RIGHT,
    ...
}
```

**Непрерывные действия (реалистичнее):**
```python
action = [
    throttle,   # [-1, 1]
    brake,      # [0, 1]
    steer       # [-1, 1]
]
```

### 4.5 Reward function (функция награды)

**Критически важно для успеха обучения!**

```python
def calculate_reward(state, action, next_state):
    reward = 0.0
    
    # 1. Награда за движение вперед
    progress = distance_to_next_checkpoint_before - distance_to_next_checkpoint_after
    reward += progress * 10.0
    
    # 2. Награда за скорость
    reward += next_state.speed * 0.1
    
    # 3. Штраф за столкновение
    if collision_detected:
        reward -= 100.0
        
    # 4. Штраф за выезд за пределы
    if out_of_bounds:
        reward -= 50.0
        
    # 5. Бонус за прохождение чекпоинта
    if checkpoint_passed:
        reward += 200.0
        
    # 6. Штраф за время (чтобы ехал быстрее)
    reward -= 0.01
    
    # 7. Штраф за резкие повороты
    reward -= abs(action.steer) * 0.05
    
    return reward
```

### 4.6 Пример обучения (Python + Stable-Baselines3)

```python
import gymnasium as gym
from stable_baselines3 import PPO
import numpy as np
import requests

class CarSimEnv(gym.Env):
    def __init__(self, server_url="http://localhost:8080"):
        super().__init__()
        self.server = server_url
        
        # Определяем пространства
        self.observation_space = gym.spaces.Box(
            low=-np.inf, high=np.inf, shape=(9,), dtype=np.float32
        )
        self.action_space = gym.spaces.Box(
            low=np.array([-1.0, 0.0, -1.0]),  # [throttle, brake, steer]
            high=np.array([1.0, 1.0, 1.0]),
            dtype=np.float32
        )
    
    def reset(self, seed=None):
        response = requests.post(f"{self.server}/reset")
        return np.array(response.json()['state']), {}
    
    def step(self, action):
        response = requests.post(
            f"{self.server}/step",
            json={'action': action.tolist()}
        )
        data = response.json()
        
        state = np.array(data['state'])
        reward = data['reward']
        done = data['done']
        
        return state, reward, done, False, {}

# Создаем окружение
env = CarSimEnv()

# Создаем агента PPO
model = PPO(
    "MlpPolicy",
    env,
    verbose=1,
    tensorboard_log="./logs/"
)

# Обучаем
model.learn(total_timesteps=1_000_000)

# Сохраняем
model.save("car_ai_model")

# Тестируем
obs, _ = env.reset()
for _ in range(1000):
    action, _ = model.predict(obs, deterministic=True)
    obs, reward, done, _, _ = env.step(action)
    if done:
        obs, _ = env.reset()
```

### 4.7 UI для обучения (Приоритет: СРЕДНИЙ)

**ImGui окно "AI Training":**
- [ ] Запуск/остановка обучения
- [ ] График reward over time
- [ ] График loss функции
- [ ] Текущий эпизод / общее количество
- [ ] Среднее время круга
- [ ] Процент завершенных кругов
- [ ] Кнопка "Load trained model"
- [ ] Визуализация того, что "видит" агент (lidar rays)

### 4.8 Мультиагентное обучение (Приоритет: НИЗКИЙ)
- [ ] Несколько машин на одной трассе
- [ ] Обучение избеганию столкновений
- [ ] Соревновательное обучение (гонки)
- [ ] Self-play (агент играет сам с собой)

---

## ФАЗА 5: Полировка и дополнительные фичи (2-3 недели)

### 5.1 Мультиплеер (Приоритет: СРЕДНИЙ)
- [ ] Local multiplayer (split-screen)
- [ ] Online multiplayer (WebSocket)
- [ ] Matchmaking и лобби
- [ ] Таблица лидеров

### 5.2 Кастомизация (Приоритет: НИЗКИЙ)
- [ ] Выбор цвета машины
- [ ] Разные типы машин (легкая/тяжелая, быстрая/маневренная)
- [ ] Улучшения (апгрейды)
    - Двигатель (+скорость)
    - Тормоза (+торможение)
    - Шины (+сцепление)

### 5.3 Режимы игры (Приоритет: НИЗКИЙ)
- [ ] **Time Trial** - на время
- [ ] **Championship** - серия гонок
- [ ] **Drift Mode** - оценка дрифта
- [ ] **Elimination** - выбывание последнего каждые 30 сек
- [ ] **AI vs Player** - гонка против обученного ИИ

### 5.4 Звук (Приоритет: НИЗКИЙ)
- [ ] Звук двигателя (зависит от RPM)
- [ ] Звук шин при дрифте
- [ ] Звук столкновения
- [ ] Фоновая музыка
- [ ] Библиотека: OpenAL или SDL_mixer

### 5.5 Сохранение прогресса (Приоритет: НИЗКИЙ)
- [ ] SQLite база данных
    - Лучшие времена
    - Пройденные трассы
    - Разблокированные машины
    - Настройки

---

## Технологический стек

### Текущий:
- **Язык**: C++17
- **Физика**: Box2D
- **Рендеринг**: OpenGL 3.3 + GLAD + GLFW
- **UI**: ImGui
- **Математика**: GLM

### Добавить:
- **JSON**: nlohmann/json
- **HTTP сервер**: cpp-httplib (header-only)
- **Python bindings**: pybind11 (альтернатива)
- **Звук**: OpenAL-Soft
- **База данных**: SQLite3

### Python (для AI):
- **RL библиотека**: Stable-Baselines3
- **Нейросети**: PyTorch (внутри SB3)
- **Визуализация**: TensorBoard
- **HTTP клиент**: requests

---

## Приоритизация

### Что делать в первую очередь (next 2 weeks):
1. ✅ Улучшение управляемости (уже сделано!)
2. ✅ Улучшенный зум (уже сделано!)
3. 🔥 **Базовый редактор трассы** (рисование стен, сохранение JSON)
4. 🔥 **Система чекпоинтов** (определение прохождения)
5. 🔥 **Таймер кругов** (засечка времени)

### Средний срок (1-2 месяца):
6. 🎯 UI редактора (ImGui окна)
7. 🎯 API для Python (HTTP сервер)
8. 🎯 Базовое обучение RL (PPO алгоритм)

### Долгосрочно (3+ месяца):
9. 📊 Продвинутое обучение (мультиагент, lidar)
10. 🎮 Режимы игры и мультиплеер
11. 🎨 Полировка (звук, графика, кастомизация)

---

## Структура проекта (после всех фаз)

```
car-simulator/
├── src/
│   ├── core/
│   │   ├── Application.cpp/h
│   │   ├── CarGame.cpp/h
│   │   └── main.cpp
│   ├── physics/
│   │   ├── VehicleModel.cpp/h
│   │   ├── VehicleParams.h
│   │   ├── Car.cpp/h
│   │   └── PacejkaTireModel.cpp/h
│   ├── track/
│   │   ├── Track.cpp/h
│   │   ├── TrackEditor.cpp/h
│   │   ├── Checkpoint.cpp/h
│   │   └── TrackSerializer.cpp/h
│   ├── ai/
│   │   ├── SimulationServer.cpp/h
│   │   ├── StateObserver.cpp/h
│   │   ├── RewardCalculator.cpp/h
│   │   └── LidarSensor.cpp/h
│   ├── rendering/
│   │   ├── Renderer.cpp/h
│   │   ├── Camera.cpp/h
│   │   └── ParticleSystem.cpp/h
│   └── ui/
│       ├── HUD.cpp/h
│       ├── EditorUI.cpp/h
│       └── TrainingUI.cpp/h
├── python/
│   ├── train.py
│   ├── env_wrapper.py
│   ├── evaluate.py
│   └── requirements.txt
├── assets/
│   ├── tracks/
│   │   ├── simple_oval.json
│   │   ├── figure_eight.json
│   │   └── nurburgring_simplified.json
│   ├── sprites/
│   └── sounds/
├── models/
│   ├── car_ppo_1m.zip
│   └── car_sac_2m.zip
└── docs/
    ├── ROADMAP.md (этот файл)
    ├── API.md
    └── TRAINING_GUIDE.md
```

---

## Метрики успеха

### После Фазы 2 (Редактор):
- [ ] Создано 3+ различных трассы
- [ ] Трассы сохраняются/загружаются без ошибок
- [ ] Время создания трассы < 10 минут

### После Фазы 4 (AI):
- [ ] Агент проходит простую трассу за < 1000 эпизодов
- [ ] Агент достигает human-level performance на простой трассе
- [ ] Training pipeline работает стабильно 24/7

### Финальная цель:
- [ ] Агент побеждает человека на сложной трассе
- [ ] Stable 60 FPS на средней машине
- [ ] Полностью функциональный редактор
- [ ] 10+ готовых трасс
- [ ] Документация и туториалы

---

## Риски и митигации

### Риск: Обучение AI не сходится
**Митигация:**
- Начать с простейшей трассы (прямая с одним поворотом)
- Тщательно настроить reward function
- Использовать проверенные алгоритмы (PPO)
- Логировать все метрики в TensorBoard

### Риск: Производительность падает
**Митигация:**
- Профилировать (Valgrind, perf)
- Оптимизировать рендеринг (batching)
- Headless режим для обучения (без графики)

### Риск: Сложность интеграции Python-C++
**Митигация:**
- Использовать REST API (проще дебажить)
- Тщательно тестировать каждый endpoint
- Создать простой тестовый клиент на Python

---

## Дальнейшее чтение

### Reinforcement Learning:
- OpenAI Spinning Up: https://spinningup.openai.com/
- Stable-Baselines3 docs: https://stable-baselines3.readthedocs.io/

### Tire Physics:
- Pacejka PDF в проекте: `/mnt/project/Pacejka_H_B_-_Tyre_and_Vehicle_Dynamics_-_2007.pdf`
- Marco Monster's Car Physics: http://www.asawicki.info/Mirror/Car%20Physics%20for%20Games/Car%20Physics%20for%20Games.html

### Box2D:
- Official manual: https://box2d.org/documentation/
- Tutorials: https://www.iforce2d.net/b2dtut/

---

🎯 **Следующий шаг**: Начать с Фазы 2.1 - базовый редактор трассы!