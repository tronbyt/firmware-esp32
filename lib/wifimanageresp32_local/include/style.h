#pragma once

const auto style = R"!(
    body {
      background: linear-gradient(to bottom, #808080, #c0c0c0);
      min-height: 100vh;
      background-size: cover;
      background-repeat: no-repeat;
      text-align: center;
      padding-top: 100px;
    }

    input {
      padding: 10px;
      margin: 5px;
    }

    button {
      padding: 10px 20px;
      margin-top: 10px;
    }
    .tile {
        width: 50vw;
        height: 10vh;
        border: 1px solid #729bd6;
        background-color: #808080;
        margin-left: auto;
        margin-right: auto;
        margin-bottom: 5px;
        display: block;
        transition: background-color 0.3s;
    }
    .tile:hover {
    background-color: #3498db;
}
    .tile.active {
        background-color: #3498db;
        color: #fff;
    }
    textarea {
            width: 90%;
            height: 300px;
    }
)!";