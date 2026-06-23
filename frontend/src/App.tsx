import { Navigate, Route, Routes } from "react-router-dom";
import { useAuth } from "./auth";
import Login from "./pages/Login";
import Register from "./pages/Register";
import Teacher from "./pages/Teacher";
import Student from "./pages/Student";

export default function App() {
  const { user, role, loading } = useAuth();

  if (loading) {
    return <div className="container">Загрузка…</div>;
  }

  return (
    <Routes>
      <Route
        path="/login"
        element={user ? <Navigate to="/" replace /> : <Login />}
      />
      <Route
        path="/register"
        element={user ? <Navigate to="/" replace /> : <Register />}
      />
      <Route
        path="/teacher"
        element={role === "teacher" ? <Teacher /> : <Navigate to="/" replace />}
      />
      <Route
        path="/student"
        element={role === "student" ? <Student /> : <Navigate to="/" replace />}
      />
      <Route
        path="*"
        element={
          <Navigate
            to={!user ? "/login" : role === "teacher" ? "/teacher" : "/student"}
            replace
          />
        }
      />
    </Routes>
  );
}
