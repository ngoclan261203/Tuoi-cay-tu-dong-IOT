//khai báo thư viện
const WebSocket = require("ws");         // Thư viện hỗ trợ giao tiếp WebSocket
const mongoose = require("mongoose");   // Thư viện kết nối và thao tác với MongoDB
const express = require("express");     // Framework xây dựng API và server HTTP
const cors = require("cors");           // Cho phép cross-origin (truy cập từ các domain khác)
const path = require("path");           // Quản lý đường dẫn file trong hệ thống
const http = require("http");           // Tạo server HTTP



// Kết nối MongoDB Atlas
mongoose
  .connect(
    "mongodb+srv://lan91154:lGuMtrqVizSDx38q@cluster0.cldor.mongodb.net/?retryWrites=true&w=majority&appName=Cluster0"
    //"mongodb+srv://lan91154:ngoclan1234@cluster0.cldor.mongodb.net/?retryWrites=true&w=majority&appName=Cluster0"
  )
  .then(() => console.log("Connected to MongoDB Atlas"))
  .catch((err) => console.error("Error connecting to MongoDB Atlas:", err));



// Định nghĩa schema và model cho dữ liệu điều khiển để lưu vào dâtbase
const controlDataSchema = new mongoose.Schema({
  timestamp: { type: Date, default: Date.now }, // Thời gian
  mode: String, // Chế độ bơm (auto/manual)
  pumpState: String, // Trạng thái bơm (bật/tắt)
  pumpSpeed: Number, // Tốc độ bơm
});

const ControlData = mongoose.model("ControlData", controlDataSchema);
//ControlData được sử dụng để:
//Lưu trữ dữ liệu mới vào MongoDB.
//Truy vấn dữ liệu từ MongoDB (ví dụ: lịch sử điều khiển).
//Cập nhật hoặc xóa dữ liệu trong MongoDB.



// Tạo WebSocket server cho NodeMCU
const wssNodeMCU = new WebSocket.Server({ port: 8081 });
//Dùng để tạo một WebSocket server,
// cho phép giao tiếp hai chiều (gửi và nhận dữ liệu) giữa server và client theo giao thức WebSocket.
console.log("WebSocket server for NodeMCU started on ws://localhost:8081");



// Tạo server HTTP để cung cấp API cho giao diện web
//dùng thư viện express
const app = express();

// Middleware
app.use(cors()); // Cho phép gọi API từ các domain:tên miềm khác
//CORS Cho phép client từ các domain khác (khác IP, cổng, giao thức) gọi API của server này.
//Nếu k có middleware này, trình duyệt có thể chặn yêu cầu khi client và server không cùng domain.
app.use(express.json()); // Hỗ trợ xử lý JSON từ client
//Phân tích nội dung của các request có định dạng JSON và chuyển nó thành JavaScript object.

// Route để phục vụ file index.html
//app.get():Định nghĩa một route xử lý các request HTTP GET.
//Ví dụ: Khi ng dùng truy cập đường dẫn /, server sẽ gửi file index.html từ server về cho trình duyệt hiển thị.
app.get('/', (req, res) => {
    const filePath = path.join(__dirname, 'login.html');
    res.sendFile(filePath);
});
app.get('/index', (req, res) => {
  const filePath = path.join(__dirname, 'index.html');
  res.sendFile(filePath);
});

app.get('/history', (req, res) => {
  const filePath = path.join(__dirname, 'history.html');
  res.sendFile(filePath);
});

// API để lấy dữ liệu từ MongoDB
app.get("/api/control-data", async (req, res) => {
  //Định nghĩa route /api/control-data để cung cấp dữ liệu điều khiển từ cơ sở dữ liệu MongoDB.
  try {
    const data = await ControlData.find().sort({ timestamp: -1 }); // Lấy dữ liệu mới nhất trước
    //await:Đợi kết quả từ truy vấn MongoDB 
    //ControlData lưu dữ liệu truy vấn
    //.sort({ timestamp: -1 }): Sắp xếp dữ liệu theo thời gian, bản ghi mới nhất sẽ ở đầu danh sách.
    res.json(data); // Trả về dữ liệu dưới dạng JSON
  } catch (error) {
    console.error("Error fetching control data:", error);
    res.status(500).json({ error: "Failed to fetch control data" });//phản hồi lại client
    //500 lỗi trong server
  }
});


// Khởi động server HTTP
const PORT = 81;

const server = http.createServer(app);
// truyền app vào hàm :Điều này có nghĩa là HTTP server này sẽ xử lý tất cả các yêu cầu HTTP đến server 
//thông qua các route đã được định nghĩa trong ứng dụng Express (ví dụ: /, /history, /api/control-data)
const wssWeb = new WebSocket.Server({ server });
//Dòng này khởi tạo một WebSocket server và liên kết nó với server HTTP mà bạn vừa tạo.
server.listen(PORT, () => {
  console.log(`HTTP server started on http://localhost:${PORT}`);
});



// Khai báo biến global cho WebSocket clients
let webClient = null;
let nodeMCUClient = null;

// Lưu trữ giá trị trước đó của control data
let previousControlData = {
  mode: null,
  pumpState: null,
  pumpSpeed: null,
};

// Khi client Web kết nối
wssWeb.on("connection", (ws,req) => {
  console.log(`New WebSocket connection from ${req.socket.remoteAddress}`);
  webClient = ws;

  ws.on("message", (message) => {
    const data = message.toString(); // Chuyển Buffer sang chuỗi
    console.log("Received from Web:", data);

    // Gửi đến NodeMCU nếu có kết nối
    if (nodeMCUClient) {
      nodeMCUClient.send(data);
      console.log("Forwarded to NodeMCU:", data);
    }
  });

  ws.on("close", () => {
    console.log("Web client disconnected");
    webClient = null;
  });
});



// Khi NodeMCU kết nối
wssNodeMCU.on("connection", (ws) => {
  console.log("NodeMCU connected");
  nodeMCUClient = ws;

  ws.on("message", async (message) => {
    const data = message.toString(); // Chuyển Buffer sang chuỗi
    console.log("Received from NodeMCU:", data);

    try {
      // Parse dữ liệu từ NodeMCU
      const jsonData = JSON.parse(data);

      // Kiểm tra nếu có đủ các trường dữ liệu cần thiết
      if (
        jsonData.hasOwnProperty("state") &&
        jsonData.hasOwnProperty("speed") &&
        jsonData.hasOwnProperty("mode")
      ) {
        const { mode, state, speed } = jsonData;

        // Kiểm tra sự thay đổi của các thông số trước khi lưu
        if (
          mode !== previousControlData.mode ||
          state !== previousControlData.pumpState ||
          speed !== previousControlData.pumpSpeed
        ) {
          // Lưu vào MongoDB nếu có thay đổi
          const newControlData = new ControlData({
            mode: mode || "manual", // Mặc định là "manual" nếu không có
            pumpState: state || "off", // Mặc định là "off" nếu không có
            pumpSpeed: speed || 0, // Mặc định là 0 nếu không có
          });

          await newControlData.save();
          console.log("Control data saved to MongoDB:", newControlData);

          // Cập nhật giá trị trước đó
          previousControlData = {
            mode,
            pumpState: state,
            pumpSpeed: speed,
          };
        } else {
          console.log("No change in control data. Skipping save.");
        }
      } else {
        console.log("Data is missing required fields. Ignored.");
      }
    } catch (err) {
      console.error("Error parsing or saving data:", err);
    }

    // Gửi dữ liệu tới Web nếu có kết nối
    if (webClient && webClient.readyState === WebSocket.OPEN) {
      webClient.send(data);
      console.log("Forwarded to Web:", data);
    }
  });

  ws.on("close", () => {
    console.log("NodeMCU disconnected");
    nodeMCUClient = null;
  });
});

